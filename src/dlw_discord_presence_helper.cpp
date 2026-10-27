#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "dlw_discord_presence_api.h"

namespace {

constexpr std::uint32_t kHandshakeOpcode = 0;
constexpr std::uint32_t kFrameOpcode = 1;
constexpr std::uint32_t kCloseOpcode = 2;
constexpr std::uint32_t kPingOpcode = 3;
constexpr std::uint32_t kPongOpcode = 4;
constexpr std::uint32_t kMaximumPayloadBytes = 64U * 1024U;
constexpr std::uint32_t kReadyTimeoutMs = 500;
constexpr std::uint32_t kIoTimeoutMs = 500;
constexpr std::size_t kMaximumPendingResponses = 8;

#if defined(DLUMA_DISCORD_RPC_TEST_PIPE)
constexpr wchar_t kPipeTemplate[] =
    L"\\\\.\\pipe\\dluma-discord-test-ipc-0";
#else
constexpr wchar_t kPipeTemplate[] =
    L"\\\\.\\pipe\\discord-ipc-0";
#endif

#pragma pack(push, 1)
struct DiscordFrameHeader {
    std::uint32_t opcode;
    std::uint32_t length;
};
#pragma pack(pop)

enum class PendingKind : std::uint32_t {
    Publish,
    Clear
};

struct PendingResponse {
    std::string nonce;
    PendingKind kind = PendingKind::Publish;
    bool occupied = false;
};

std::mutex g_mutex;
HANDLE g_pipe = INVALID_HANDLE_VALUE;
std::uint64_t g_application_id = 0;
std::uint64_t g_nonce = 0;
std::array<PendingResponse, kMaximumPendingResponses> g_pending{};
std::uint32_t g_pending_count = 0;
bool g_initialized = false;
bool g_connected = false;
bool g_ready = false;
bool g_clear_pending = false;
bool g_last_operation_succeeded = false;
char g_last_error[DLUMA_DISCORD_STATUS_TEXT_CAPACITY]{};

template <std::size_t Capacity>
bool HasTerminator(const char (&value)[Capacity])
{
    return std::memchr(value, '\0', Capacity) != nullptr;
}

bool IsValidButtonLabel(const char* value)
{
    const unsigned char* cursor =
        reinterpret_cast<const unsigned char*>(value);
    std::size_t codepoints = 0;
    if (!cursor || !*cursor) return false;
    while (*cursor) {
        std::uint32_t scalar = 0;
        std::size_t consumed = 0;
        if (cursor[0] < 0x80U) {
            scalar = cursor[0];
            consumed = 1;
        } else if (cursor[0] >= 0xC2U && cursor[0] <= 0xDFU &&
                   (cursor[1] & 0xC0U) == 0x80U) {
            scalar = ((cursor[0] & 0x1FU) << 6) |
                     (cursor[1] & 0x3FU);
            consumed = 2;
        } else if (cursor[0] >= 0xE0U && cursor[0] <= 0xEFU &&
                   (cursor[1] & 0xC0U) == 0x80U &&
                   (cursor[2] & 0xC0U) == 0x80U &&
                   !(cursor[0] == 0xE0U && cursor[1] < 0xA0U) &&
                   !(cursor[0] == 0xEDU && cursor[1] >= 0xA0U)) {
            scalar = ((cursor[0] & 0x0FU) << 12) |
                     ((cursor[1] & 0x3FU) << 6) |
                     (cursor[2] & 0x3FU);
            consumed = 3;
        } else if (cursor[0] >= 0xF0U && cursor[0] <= 0xF4U &&
                   (cursor[1] & 0xC0U) == 0x80U &&
                   (cursor[2] & 0xC0U) == 0x80U &&
                   (cursor[3] & 0xC0U) == 0x80U &&
                   !(cursor[0] == 0xF0U && cursor[1] < 0x90U) &&
                   !(cursor[0] == 0xF4U && cursor[1] >= 0x90U)) {
            scalar = ((cursor[0] & 0x07U) << 18) |
                     ((cursor[1] & 0x3FU) << 12) |
                     ((cursor[2] & 0x3FU) << 6) |
                     (cursor[3] & 0x3FU);
            consumed = 4;
        } else {
            return false;
        }
        if (scalar < 0x20U ||
            (scalar >= 0x7FU && scalar <= 0x9FU) ||
            scalar == 0x2028U || scalar == 0x2029U ||
            ++codepoints > 32U)
            return false;
        cursor += consumed;
    }
    return codepoints != 0;
}

bool IsWorkshopItemUrl(const char* value)
{
    constexpr char prefix[] =
        "https://steamcommunity.com/sharedfiles/filedetails/?id=";
    const char* cursor;
    std::uint64_t parsed = 0;
    std::size_t digits = 0;
    if (!value ||
        std::strncmp(value, prefix, sizeof(prefix) - 1U) != 0)
        return false;
    cursor = value + sizeof(prefix) - 1U;
    while (*cursor) {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(*cursor - '0');
        if (*cursor < '0' || *cursor > '9' || digits >= 20U ||
            parsed > (UINT64_MAX - digit) / 10U)
            return false;
        parsed = parsed * 10U + digit;
        ++digits;
        ++cursor;
    }
    return digits != 0 && parsed != 0;
}

template <std::size_t Capacity>
void CopyBounded(char (&destination)[Capacity], const std::string& source)
{
    const std::size_t count =
        source.size() < Capacity - 1 ? source.size() : Capacity - 1;
    if (count) std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

template <std::size_t Capacity>
void CopyBounded(char (&destination)[Capacity], const char* source)
{
    std::size_t count = 0;
    if (source) {
        while (source[count] && count + 1 < Capacity) ++count;
        if (count) std::memcpy(destination, source, count);
    }
    destination[count] = '\0';
}

void SetErrorLocked(const char* message)
{
    g_last_operation_succeeded = false;
    CopyBounded(
        g_last_error,
        message ? message : "Discord desktop RPC failed");
}

void SetSuccessLocked()
{
    g_last_operation_succeeded = true;
    g_last_error[0] = '\0';
}

void ResetPendingLocked()
{
    for (PendingResponse& pending : g_pending) {
        pending.nonce.clear();
        pending.occupied = false;
    }
    g_pending_count = 0;
}

void ClosePipeLocked()
{
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_pipe, nullptr);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    g_connected = false;
    g_ready = false;
}

void ResetConnectionLocked()
{
    ClosePipeLocked();
    ResetPendingLocked();
    g_initialized = false;
    g_clear_pending = false;
}

bool AppendDecimal(std::string& output, std::uint64_t value)
{
    char reverse[32]{};
    std::size_t count = 0;
    do {
        reverse[count++] =
            static_cast<char>('0' + static_cast<char>(value % 10));
        value /= 10;
    } while (value && count < sizeof(reverse));
    if (value) return false;
    while (count) output.push_back(reverse[--count]);
    return true;
}

bool AppendJsonString(std::string& output, const char* value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    const unsigned char* cursor =
        reinterpret_cast<const unsigned char*>(value);
    if (!value) return false;
    output.push_back('"');
    while (*cursor) {
        const unsigned char current = *cursor++;
        switch (current) {
        case '"': output.append("\\\""); break;
        case '\\': output.append("\\\\"); break;
        case '\b': output.append("\\b"); break;
        case '\f': output.append("\\f"); break;
        case '\n': output.append("\\n"); break;
        case '\r': output.append("\\r"); break;
        case '\t': output.append("\\t"); break;
        default:
            if (current < 0x20) {
                output.append("\\u00");
                output.push_back(kHex[(current >> 4) & 0x0f]);
                output.push_back(kHex[current & 0x0f]);
            } else {
                output.push_back(static_cast<char>(current));
            }
            break;
        }
        if (output.size() > kMaximumPayloadBytes) return false;
    }
    output.push_back('"');
    return output.size() <= kMaximumPayloadBytes;
}

std::string NextNonceLocked()
{
    std::string nonce;
    nonce.reserve(48);
    AppendDecimal(nonce, static_cast<std::uint64_t>(GetCurrentProcessId()));
    nonce.push_back('-');
    AppendDecimal(nonce, ++g_nonce);
    return nonce;
}

bool WaitOverlapped(
    HANDLE pipe, OVERLAPPED& overlapped, DWORD timeout_ms,
    DWORD* transferred)
{
    const DWORD wait_result =
        WaitForSingleObject(overlapped.hEvent, timeout_ms);
    if (wait_result != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        WaitForSingleObject(overlapped.hEvent, INFINITE);
        return false;
    }
    return GetOverlappedResult(
        pipe, &overlapped, transferred, FALSE) != FALSE;
}

bool WriteChunkLocked(const void* data, DWORD size, DWORD* written)
{
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;
    DWORD transferred = 0;
    BOOL result = WriteFile(
        g_pipe, data, size, &transferred, &overlapped);
    if (!result && GetLastError() == ERROR_IO_PENDING)
        result = WaitOverlapped(
            g_pipe, overlapped, kIoTimeoutMs, &transferred);
    CloseHandle(overlapped.hEvent);
    if (written) *written = transferred;
    return result != FALSE;
}

bool ReadChunkLocked(void* data, DWORD size, DWORD* read)
{
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;
    DWORD transferred = 0;
    BOOL result = ReadFile(
        g_pipe, data, size, &transferred, &overlapped);
    if (!result && GetLastError() == ERROR_IO_PENDING)
        result = WaitOverlapped(
            g_pipe, overlapped, kIoTimeoutMs, &transferred);
    CloseHandle(overlapped.hEvent);
    if (read) *read = transferred;
    return result != FALSE;
}

bool WriteAllLocked(const void* data, std::uint32_t size)
{
    const std::uint8_t* cursor =
        static_cast<const std::uint8_t*>(data);
    std::uint32_t remaining = size;
    while (remaining) {
        DWORD written = 0;
        if (!WriteChunkLocked(cursor, remaining, &written) ||
            !written || written > remaining)
            return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool ReadAllLocked(void* data, std::uint32_t size)
{
    std::uint8_t* cursor = static_cast<std::uint8_t*>(data);
    std::uint32_t remaining = size;
    while (remaining) {
        DWORD read = 0;
        if (!ReadChunkLocked(cursor, remaining, &read) ||
            !read || read > remaining)
            return false;
        cursor += read;
        remaining -= read;
    }
    return true;
}

bool WriteFrameLocked(
    std::uint32_t opcode, const std::string& payload)
{
    if (!g_connected || g_pipe == INVALID_HANDLE_VALUE ||
        payload.size() > kMaximumPayloadBytes)
        return false;
    const DiscordFrameHeader header{
        opcode, static_cast<std::uint32_t>(payload.size())};
    return WriteAllLocked(&header, sizeof(header)) &&
           (payload.empty() ||
            WriteAllLocked(payload.data(), header.length));
}

bool JsonStringField(
    const std::string& payload, const char* key,
    std::string& value)
{
    std::string marker;
    marker.reserve(std::strlen(key) + 2);
    marker.push_back('"');
    marker.append(key);
    marker.push_back('"');
    std::size_t position = payload.find(marker);
    if (position == std::string::npos) return false;
    position += marker.size();
    while (position < payload.size() &&
           std::isspace(
               static_cast<unsigned char>(payload[position])))
        ++position;
    if (position >= payload.size() || payload[position++] != ':')
        return false;
    while (position < payload.size() &&
           std::isspace(
               static_cast<unsigned char>(payload[position])))
        ++position;
    if (position >= payload.size() || payload[position++] != '"')
        return false;
    value.clear();
    while (position < payload.size()) {
        const char current = payload[position++];
        if (current == '"') return true;
        if (current == '\\') {
            if (position >= payload.size()) return false;
            const char escaped = payload[position++];
            switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: return false;
            }
        } else {
            value.push_back(current);
        }
        if (value.size() > DLUMA_DISCORD_STATUS_TEXT_CAPACITY)
            return false;
    }
    return false;
}

PendingKind RemovePendingLocked(
    const std::string& nonce, bool* found)
{
    if (found) *found = false;
    for (PendingResponse& pending : g_pending) {
        if (pending.occupied && pending.nonce == nonce) {
            const PendingKind kind = pending.kind;
            pending.nonce.clear();
            pending.occupied = false;
            if (g_pending_count) --g_pending_count;
            if (found) *found = true;
            return kind;
        }
    }
    return PendingKind::Publish;
}

bool AddPendingLocked(
    const std::string& nonce, PendingKind kind)
{
    for (PendingResponse& pending : g_pending) {
        if (!pending.occupied) {
            pending.nonce = nonce;
            pending.kind = kind;
            pending.occupied = true;
            ++g_pending_count;
            return true;
        }
    }
    return false;
}

void FinishClearIfDrainedLocked()
{
    if (g_clear_pending && !g_pending_count) {
        ClosePipeLocked();
        g_clear_pending = false;
    }
}

bool HandleFrameLocked(
    std::uint32_t opcode, const std::string& payload)
{
    if (opcode == kPingOpcode)
        return WriteFrameLocked(kPongOpcode, payload);
    if (opcode == kCloseOpcode) {
        if (g_clear_pending) {
            ResetConnectionLocked();
            SetSuccessLocked();
            return true;
        }
        ResetConnectionLocked();
        SetErrorLocked("Discord desktop closed the RPC connection");
        return false;
    }
    if (opcode != kFrameOpcode) {
        ResetConnectionLocked();
        SetErrorLocked("Discord desktop sent an invalid RPC opcode");
        return false;
    }

    std::string event;
    if (JsonStringField(payload, "evt", event) &&
        event == "READY") {
        g_ready = true;
        g_initialized = true;
        SetSuccessLocked();
        return true;
    }

    std::string nonce;
    bool found = false;
    PendingKind kind = PendingKind::Publish;
    if (JsonStringField(payload, "nonce", nonce))
        kind = RemovePendingLocked(nonce, &found);
    if (JsonStringField(payload, "evt", event) &&
        event == "ERROR") {
        SetErrorLocked(
            kind == PendingKind::Clear
                ? "Discord rejected the Rich Presence clear"
                : "Discord rejected the Rich Presence update");
    } else if (found) {
        SetSuccessLocked();
    }
    FinishClearIfDrainedLocked();
    return true;
}

bool PumpLocked()
{
    if (!g_connected || g_pipe == INVALID_HANDLE_VALUE)
        return g_clear_pending || !g_initialized;
    for (unsigned int frame_count = 0;
         frame_count < 32; ++frame_count) {
        DiscordFrameHeader header{};
        DWORD available = 0;
        DWORD peeked = 0;
        if (!PeekNamedPipe(
                g_pipe, &header, sizeof(header),
                &peeked, &available, nullptr)) {
            if (g_clear_pending) {
                ResetConnectionLocked();
                SetSuccessLocked();
                return true;
            }
            ResetConnectionLocked();
            SetErrorLocked("Discord desktop RPC connection was lost");
            return false;
        }
        if (available < sizeof(header) || peeked < sizeof(header))
            return true;
        if (header.length > kMaximumPayloadBytes ||
            available < sizeof(header) + header.length) {
            if (header.length <= kMaximumPayloadBytes)
                return true;
            ResetConnectionLocked();
            SetErrorLocked("Discord desktop sent an oversized RPC frame");
            return false;
        }
        if (!ReadAllLocked(&header, sizeof(header))) {
            ResetConnectionLocked();
            SetErrorLocked("Discord desktop RPC header read failed");
            return false;
        }
        std::string payload(header.length, '\0');
        if (header.length &&
            !ReadAllLocked(payload.data(), header.length)) {
            ResetConnectionLocked();
            SetErrorLocked("Discord desktop RPC payload read failed");
            return false;
        }
        if (!HandleFrameLocked(header.opcode, payload))
            return false;
        if (!g_connected) return true;
    }
    return true;
}

bool OpenDiscordPipeLocked()
{
    for (unsigned int index = 0; index < 10; ++index) {
        wchar_t pipe_name[sizeof(kPipeTemplate) / sizeof(wchar_t)]{};
        std::memcpy(pipe_name, kPipeTemplate, sizeof(kPipeTemplate));
        pipe_name[(sizeof(kPipeTemplate) / sizeof(wchar_t)) - 2] =
            static_cast<wchar_t>(L'0' + index);
        HANDLE candidate = CreateFileW(
            pipe_name,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        if (candidate != INVALID_HANDLE_VALUE) {
            g_pipe = candidate;
            g_connected = true;
            return true;
        }
    }
    return false;
}

bool SendHandshakeLocked()
{
    std::string payload("{\"v\":1,\"client_id\":\"");
    if (!AppendDecimal(payload, g_application_id)) return false;
    payload.append("\"}");
    return WriteFrameLocked(kHandshakeOpcode, payload);
}

bool BuildActivityPayloadLocked(
    const DlumaDiscordActivityV1& activity,
    const std::string& nonce,
    std::string& payload)
{
    payload.clear();
    payload.reserve(4096);
    payload.append(
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":");
    if (!AppendDecimal(
            payload,
            static_cast<std::uint64_t>(GetCurrentProcessId())))
        return false;
    payload.append(
        ",\"activity\":{\"type\":0,\"status_display_type\":2,"
        "\"details\":");
    if (!AppendJsonString(payload, activity.details)) return false;
    if (activity.flags & DLUMA_DISCORD_ACTIVITY_HAS_STATE) {
        payload.append(",\"state\":");
        if (!AppendJsonString(payload, activity.state)) return false;
    }
    if (activity.flags &
        DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP) {
        payload.append(",\"timestamps\":{\"start\":");
        if (!AppendDecimal(
                payload, activity.start_timestamp_seconds))
            return false;
        payload.push_back('}');
    }
    payload.append(",\"assets\":{\"large_image\":");
    if (!AppendJsonString(payload, activity.large_image)) return false;
    if (std::strlen(activity.large_text) >= 2) {
        payload.append(",\"large_text\":");
        if (!AppendJsonString(payload, activity.large_text))
            return false;
    }
    if (activity.flags & DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE) {
        payload.append(",\"small_image\":");
        if (!AppendJsonString(payload, activity.small_image))
            return false;
        payload.append(",\"small_text\":");
        if (!AppendJsonString(payload, activity.small_text))
            return false;
    }
    payload.push_back('}');
    if (activity.flags & DLUMA_DISCORD_ACTIVITY_HAS_BUTTON) {
        payload.append(",\"buttons\":[{\"label\":");
        if (!AppendJsonString(payload, activity.button_label))
            return false;
        payload.append(",\"url\":");
        if (!AppendJsonString(payload, activity.button_url))
            return false;
        payload.append("}]");
    }
    payload.append("}},\"nonce\":");
    if (!AppendJsonString(payload, nonce.c_str())) return false;
    payload.push_back('}');
    return payload.size() <= kMaximumPayloadBytes;
}

bool BuildClearPayloadLocked(
    const std::string& nonce, std::string& payload)
{
    payload.clear();
    payload.reserve(160);
    payload.append(
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":");
    if (!AppendDecimal(
            payload,
            static_cast<std::uint64_t>(GetCurrentProcessId())))
        return false;
    payload.append(",\"activity\":null},\"nonce\":");
    if (!AppendJsonString(payload, nonce.c_str())) return false;
    payload.push_back('}');
    return true;
}

int DLUMA_DISCORD_CALL Initialize(std::uint64_t application_id)
{
    try {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!application_id) {
            SetErrorLocked("Discord application ID is invalid");
            return 0;
        }
        if (g_ready && g_initialized &&
            application_id == g_application_id) {
            SetSuccessLocked();
            return 1;
        }
        if (g_clear_pending) {
            SetErrorLocked(
                "The previous Discord RPC clear is still draining");
            return 0;
        }
        ResetConnectionLocked();
        g_application_id = application_id;
        if (!OpenDiscordPipeLocked()) {
            SetErrorLocked("Discord desktop client is not running");
            return 0;
        }
        if (!SendHandshakeLocked()) {
            ResetConnectionLocked();
            SetErrorLocked(
                "Discord desktop RPC handshake write failed");
            return 0;
        }
        g_initialized = true;
        const ULONGLONG deadline =
            GetTickCount64() + kReadyTimeoutMs;
        while (!g_ready) {
            if (!PumpLocked()) return 0;
            if (g_ready) break;
            if (GetTickCount64() >= deadline) {
                ResetConnectionLocked();
                SetErrorLocked(
                    "Discord desktop RPC READY timed out");
                return 0;
            }
            Sleep(1);
        }
        SetSuccessLocked();
        return 1;
    } catch (...) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ResetConnectionLocked();
        SetErrorLocked("Discord desktop RPC initialization failed");
        return 0;
    }
}

int DLUMA_DISCORD_CALL Publish(const DlumaDiscordActivityV1* source)
{
    if (!source || source->struct_size != sizeof(DlumaDiscordActivityV1) ||
        (source->flags & ~(DLUMA_DISCORD_ACTIVITY_HAS_STATE |
                           DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE |
                           DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP |
                           DLUMA_DISCORD_ACTIVITY_HAS_BUTTON)) != 0 ||
        !HasTerminator(source->details) ||
        !HasTerminator(source->state) ||
        !HasTerminator(source->large_image) ||
        !HasTerminator(source->large_text) ||
        !HasTerminator(source->small_image) ||
        !HasTerminator(source->small_text) ||
        !HasTerminator(source->button_label) ||
        !HasTerminator(source->button_url) ||
        std::strlen(source->details) < 2 ||
        !source->large_image[0] ||
        ((source->flags & DLUMA_DISCORD_ACTIVITY_HAS_STATE) &&
         std::strlen(source->state) < 2) ||
        ((source->flags &
          DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP) &&
         !source->start_timestamp_seconds) ||
        ((source->flags & DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE) &&
         (!source->small_image[0] ||
          std::strlen(source->small_text) < 2)) ||
        ((source->flags & DLUMA_DISCORD_ACTIVITY_HAS_BUTTON) &&
         (!IsValidButtonLabel(source->button_label) ||
          !IsWorkshopItemUrl(source->button_url))))
        return 0;

    try {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!g_initialized || !g_connected || !g_ready ||
            g_clear_pending) {
            SetErrorLocked("Discord desktop RPC is not ready");
            return 0;
        }
        /*
         * Always reserve one response slot for the immediate clear used by
         * disable and shutdown, even when Discord stops acknowledging
         * publishes.
         */
        if (g_pending_count >= kMaximumPendingResponses - 1) {
            SetErrorLocked(
                "Discord desktop RPC publish response queue is full");
            return 0;
        }
        const std::string nonce = NextNonceLocked();
        std::string payload;
        if (!BuildActivityPayloadLocked(*source, nonce, payload) ||
            !WriteFrameLocked(kFrameOpcode, payload) ||
            !AddPendingLocked(nonce, PendingKind::Publish)) {
            ResetConnectionLocked();
            SetErrorLocked("Discord Rich Presence update write failed");
            return 0;
        }
        return 1;
    } catch (...) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ResetConnectionLocked();
        SetErrorLocked("Discord Rich Presence update allocation failed");
        return 0;
    }
}

int DLUMA_DISCORD_CALL Clear()
{
    try {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!g_connected || g_pipe == INVALID_HANDLE_VALUE) {
            ResetConnectionLocked();
            SetSuccessLocked();
            return 1;
        }
        if (g_clear_pending) return 1;
        if (g_pending_count >= kMaximumPendingResponses) {
            SetErrorLocked("Discord desktop RPC response queue is full");
            return 0;
        }
        const std::string nonce = NextNonceLocked();
        std::string payload;
        if (!BuildClearPayloadLocked(nonce, payload) ||
            !WriteFrameLocked(kFrameOpcode, payload) ||
            !AddPendingLocked(nonce, PendingKind::Clear)) {
            ResetConnectionLocked();
            SetErrorLocked("Discord Rich Presence clear write failed");
            return 0;
        }
        g_initialized = false;
        g_clear_pending = true;
        return 1;
    } catch (...) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ResetConnectionLocked();
        SetErrorLocked("Discord Rich Presence clear allocation failed");
        return 0;
    }
}

int DLUMA_DISCORD_CALL Pump()
{
    try {
        std::lock_guard<std::mutex> guard(g_mutex);
        return PumpLocked() ? 1 : 0;
    } catch (...) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ResetConnectionLocked();
        SetErrorLocked("Discord desktop RPC pump failed");
        return 0;
    }
}

int DLUMA_DISCORD_CALL Status(DlumaDiscordPresenceStatusV1* status)
{
    if (!status || status->struct_size != sizeof(*status)) return 0;
    try {
        std::lock_guard<std::mutex> guard(g_mutex);
        status->initialized = g_initialized ? 1u : 0u;
        status->connected = g_connected ? 1u : 0u;
        status->ready = g_ready ? 1u : 0u;
        status->responses_pending = g_pending_count;
        status->last_operation_succeeded =
            g_last_operation_succeeded ? 1u : 0u;
        status->rpc_protocol_version =
            DLUMA_DISCORD_RPC_PROTOCOL_VERSION;
        CopyBounded(
            status->transport_release,
            DLUMA_DISCORD_RPC_TRANSPORT_RELEASE);
        CopyBounded(status->last_error, g_last_error);
        return 1;
    } catch (...) {
        return 0;
    }
}

int DLUMA_DISCORD_CALL Quiesce(std::uint32_t timeout_ms)
{
    try {
        const ULONGLONG deadline = GetTickCount64() + timeout_ms;
        if (!Clear()) return 0;
        for (;;) {
            {
                std::lock_guard<std::mutex> guard(g_mutex);
                if (!g_pending_count) {
                    ResetConnectionLocked();
                    SetSuccessLocked();
                    return 1;
                }
                if (!PumpLocked()) return 0;
                if (!g_pending_count) {
                    ResetConnectionLocked();
                    SetSuccessLocked();
                    return 1;
                }
            }
            if (GetTickCount64() >= deadline) return 0;
            Sleep(1);
        }
    } catch (...) {
        return 0;
    }
}

const DlumaDiscordPresenceApiV1 g_api = {
    sizeof(DlumaDiscordPresenceApiV1),
    DLUMA_DISCORD_PRESENCE_ABI_VERSION,
    DLUMA_DISCORD_PRESENCE_HELPER_RELEASE,
    DLUMA_DISCORD_RPC_TRANSPORT_RELEASE,
    DLUMA_DISCORD_RPC_PROTOCOL_VERSION,
    &Initialize,
    &Publish,
    &Clear,
    &Pump,
    &Status,
    &Quiesce
};

}  // namespace

extern "C" DLUMA_DISCORD_EXPORT int DLUMA_DISCORD_CALL
DLUMA_DiscordPresence_GetApi(
    std::uint32_t requested_abi,
    std::uint32_t caller_table_size,
    const DlumaDiscordPresenceApiV1** api_out)
{
    if (!api_out) return 0;
    *api_out = nullptr;
    if (requested_abi != DLUMA_DISCORD_PRESENCE_ABI_VERSION ||
        caller_table_size != sizeof(DlumaDiscordPresenceApiV1))
        return 0;
    *api_out = &g_api;
    return 1;
}
