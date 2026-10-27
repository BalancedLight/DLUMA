#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

#include "../src/dlw_discord_presence_api.h"

namespace {

constexpr wchar_t kTestPipeName[] =
    L"\\\\.\\pipe\\dluma-discord-test-ipc-0";
constexpr std::uint32_t kHandshakeOpcode = 0;
constexpr std::uint32_t kFrameOpcode = 1;
constexpr std::uint32_t kPingOpcode = 3;
constexpr std::uint32_t kPongOpcode = 4;

#pragma pack(push, 1)
struct FrameHeader {
    std::uint32_t opcode;
    std::uint32_t length;
};
#pragma pack(pop)

enum class ServerMode {
    Normal,
    RejectPublish,
    HoldReplies,
    OversizedReady
};

struct TestServer {
    ServerMode mode = ServerMode::Normal;
    HANDLE ready_event = nullptr;
    HANDLE release_event = nullptr;
    HANDLE thread = nullptr;
    std::mutex mutex;
    std::string last_activity;
    volatile LONG failed = 0;
    volatile LONG pong_count = 0;
    volatile LONG clear_count = 0;
};

bool ReadAll(HANDLE pipe, void* data, DWORD size)
{
    std::uint8_t* cursor = static_cast<std::uint8_t*>(data);
    DWORD remaining = size;
    while (remaining) {
        DWORD read = 0;
        if (!ReadFile(pipe, cursor, remaining, &read, nullptr) ||
            !read || read > remaining)
            return false;
        cursor += read;
        remaining -= read;
    }
    return true;
}

bool WriteAll(HANDLE pipe, const void* data, DWORD size)
{
    const std::uint8_t* cursor =
        static_cast<const std::uint8_t*>(data);
    DWORD remaining = size;
    while (remaining) {
        DWORD written = 0;
        if (!WriteFile(pipe, cursor, remaining, &written, nullptr) ||
            !written || written > remaining)
            return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool ReadFrame(
    HANDLE pipe, std::uint32_t& opcode, std::string& payload)
{
    FrameHeader header{};
    if (!ReadAll(pipe, &header, sizeof(header)) ||
        header.length > 64U * 1024U)
        return false;
    payload.assign(header.length, '\0');
    if (header.length &&
        !ReadAll(pipe, payload.data(), header.length))
        return false;
    opcode = header.opcode;
    return true;
}

bool WriteFrame(
    HANDLE pipe, std::uint32_t opcode,
    const std::string& payload)
{
    const FrameHeader header{
        opcode, static_cast<std::uint32_t>(payload.size())};
    return WriteAll(pipe, &header, sizeof(header)) &&
           (payload.empty() ||
            WriteAll(
                pipe, payload.data(),
                static_cast<DWORD>(payload.size())));
}

std::string ExtractNonce(const std::string& payload)
{
    const std::string marker = "\"nonce\":\"";
    const std::size_t start = payload.find(marker);
    if (start == std::string::npos) return {};
    const std::size_t value_start = start + marker.size();
    const std::size_t end = payload.find('"', value_start);
    if (end == std::string::npos) return {};
    return payload.substr(value_start, end - value_start);
}

DWORD WINAPI ServerThread(void* parameter)
{
    TestServer& server = *static_cast<TestServer*>(parameter);
    HANDLE pipe = CreateNamedPipeW(
        kTestPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 64U * 1024U, 64U * 1024U, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&server.failed, 1);
        SetEvent(server.ready_event);
        return 0;
    }
    SetEvent(server.ready_event);
    if (!ConnectNamedPipe(pipe, nullptr) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        InterlockedExchange(&server.failed, 1);
        CloseHandle(pipe);
        return 0;
    }

    std::uint32_t opcode = 0;
    std::string payload;
    if (!ReadFrame(pipe, opcode, payload) ||
        opcode != kHandshakeOpcode ||
        payload.find(
            "\"client_id\":\"1531147761955766444\"") ==
            std::string::npos) {
        InterlockedExchange(&server.failed, 1);
        CloseHandle(pipe);
        return 0;
    }

    if (server.mode == ServerMode::OversizedReady) {
        const FrameHeader oversized{
            kFrameOpcode, 64U * 1024U + 1U};
        if (!WriteAll(pipe, &oversized, sizeof(oversized)))
            InterlockedExchange(&server.failed, 1);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return 0;
    }

    if (!WriteFrame(
            pipe, kFrameOpcode,
            "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\","
            "\"data\":{\"v\":1}}")) {
        InterlockedExchange(&server.failed, 1);
        CloseHandle(pipe);
        return 0;
    }

    bool sent_ping = false;
    for (;;) {
        if (!ReadFrame(pipe, opcode, payload)) break;
        if (opcode == kPongOpcode) {
            InterlockedIncrement(&server.pong_count);
            continue;
        }
        if (opcode != kFrameOpcode ||
            payload.find("\"cmd\":\"SET_ACTIVITY\"") ==
                std::string::npos) {
            InterlockedExchange(&server.failed, 1);
            break;
        }
        const bool clearing =
            payload.find("\"activity\":null") != std::string::npos;
        if (clearing)
            InterlockedIncrement(&server.clear_count);
        else {
            std::lock_guard<std::mutex> guard(server.mutex);
            server.last_activity = payload;
        }

        if (!sent_ping) {
            sent_ping = true;
            if (!WriteFrame(
                    pipe, kPingOpcode, "{\"probe\":1}")) {
                InterlockedExchange(&server.failed, 1);
                break;
            }
        }
        if (server.mode == ServerMode::HoldReplies &&
            WaitForSingleObject(
                server.release_event, 5000) != WAIT_OBJECT_0) {
            InterlockedExchange(&server.failed, 1);
            break;
        }
        const std::string nonce = ExtractNonce(payload);
        if (nonce.empty()) {
            InterlockedExchange(&server.failed, 1);
            break;
        }
        std::string response;
        if (server.mode == ServerMode::RejectPublish &&
            !clearing) {
            response =
                "{\"cmd\":\"SET_ACTIVITY\",\"evt\":\"ERROR\","
                "\"data\":{\"message\":\"rejected\"},\"nonce\":\"" +
                nonce + "\"}";
        } else {
            response =
                "{\"cmd\":\"SET_ACTIVITY\",\"data\":null,"
                "\"evt\":null,\"nonce\":\"" +
                nonce + "\"}";
        }
        if (!WriteFrame(pipe, kFrameOpcode, response)) {
            InterlockedExchange(&server.failed, 1);
            break;
        }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    return 0;
}

void StartServer(TestServer& server, ServerMode mode)
{
    server.mode = mode;
    server.ready_event =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    server.release_event =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(server.ready_event && server.release_event);
    server.thread =
        CreateThread(nullptr, 0, ServerThread, &server, 0, nullptr);
    assert(server.thread);
    assert(
        WaitForSingleObject(server.ready_event, 2000) ==
        WAIT_OBJECT_0);
    assert(
        InterlockedCompareExchange(&server.failed, 0, 0) == 0);
}

void StopServer(TestServer& server)
{
    if (server.release_event) SetEvent(server.release_event);
    if (server.thread) {
        assert(
            WaitForSingleObject(server.thread, 3000) ==
            WAIT_OBJECT_0);
        CloseHandle(server.thread);
    }
    if (server.ready_event) CloseHandle(server.ready_event);
    if (server.release_event) CloseHandle(server.release_event);
    server.thread = nullptr;
    server.ready_event = nullptr;
    server.release_event = nullptr;
    assert(
        InterlockedCompareExchange(&server.failed, 0, 0) == 0);
}

DlumaDiscordPresenceStatusV1 ReadStatus(
    const DlumaDiscordPresenceApiV1& api)
{
    DlumaDiscordPresenceStatusV1 status{};
    status.struct_size = sizeof(status);
    assert(api.status(&status));
    return status;
}

DlumaDiscordPresenceStatusV1 PumpUntilDrained(
    const DlumaDiscordPresenceApiV1& api)
{
    const ULONGLONG deadline = GetTickCount64() + 2000;
    for (;;) {
        assert(api.pump());
        DlumaDiscordPresenceStatusV1 status = ReadStatus(api);
        if (!status.responses_pending) return status;
        assert(GetTickCount64() < deadline);
        Sleep(1);
    }
}

DlumaDiscordActivityV1 MakeActivity()
{
    DlumaDiscordActivityV1 activity{};
    activity.struct_size = sizeof(activity);
    activity.flags = DLUMA_DISCORD_ACTIVITY_HAS_STATE |
        DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE |
        DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP |
        DLUMA_DISCORD_ACTIVITY_HAS_BUTTON;
    activity.start_timestamp_seconds = 1720000000ULL;
    strcpy_s(activity.details, "Map: SampleProject");
    strcpy_s(activity.state, "Quest: First Assignment");
    strcpy_s(
        activity.large_image,
        "https://steamuserimages-a.akamaihd.net/example.jpg");
    strcpy_s(activity.large_text, "SampleProject");
    strcpy_s(activity.small_image, "dl_playing");
    strcpy_s(activity.small_text, "Game is RUNNING");
    strcpy_s(activity.button_label, "View on Steam Workshop");
    strcpy_s(
        activity.button_url,
        "https://steamcommunity.com/sharedfiles/filedetails/"
        "?id=3771245504");
    return activity;
}

}  // namespace

int main()
{
    const DlumaDiscordPresenceApiV1* api = nullptr;
    assert(!DLUMA_DiscordPresence_GetApi(
        0, sizeof(DlumaDiscordPresenceApiV1), &api));
    assert(api == nullptr);
    assert(DLUMA_DiscordPresence_GetApi(
        DLUMA_DISCORD_PRESENCE_ABI_VERSION,
        sizeof(DlumaDiscordPresenceApiV1), &api));
    assert(api);
    assert(api->struct_size == sizeof(*api));
    assert(api->abi_version == DLUMA_DISCORD_PRESENCE_ABI_VERSION);
    assert(std::strcmp(
        api->transport_release,
        DLUMA_DISCORD_RPC_TRANSPORT_RELEASE) == 0);
    assert(
        api->rpc_protocol_version ==
        DLUMA_DISCORD_RPC_PROTOCOL_VERSION);

    DlumaDiscordActivityV1 malformed{};
    assert(!api->publish(&malformed));
    malformed.struct_size = sizeof(malformed);
    std::memset(malformed.details, 'x', sizeof(malformed.details));
    assert(!api->publish(&malformed));
    assert(!api->initialize(0));
    assert(!api->initialize(1531147761955766444ULL));

    TestServer normal;
    StartServer(normal, ServerMode::Normal);
    assert(api->initialize(1531147761955766444ULL));
    DlumaDiscordPresenceStatusV1 status = ReadStatus(*api);
    assert(status.initialized == 1);
    assert(status.connected == 1);
    assert(status.ready == 1);
    assert(status.rpc_protocol_version == 1);

    DlumaDiscordActivityV1 activity = MakeActivity();
    DlumaDiscordActivityV1 invalid_timestamp = activity;
    invalid_timestamp.start_timestamp_seconds = 0;
    assert(!api->publish(&invalid_timestamp));
    DlumaDiscordActivityV1 invalid_button = activity;
    strcpy_s(
        invalid_button.button_url,
        "https://example.com/sharedfiles/filedetails/?id=3771245504");
    assert(!api->publish(&invalid_button));
    DlumaDiscordActivityV1 oversized_button_label = activity;
    strcpy_s(
        oversized_button_label.button_label,
        "123456789012345678901234567890123");
    assert(!api->publish(&oversized_button_label));
    DlumaDiscordActivityV1 unicode_button_label = activity;
    for (std::size_t index = 0; index < 32; ++index) {
        std::memcpy(
            unicode_button_label.button_label + index * 4,
            "\xF0\x9F\x8E\xAE", 4);
    }
    unicode_button_label.button_label[128] = '\0';
    assert(api->publish(&unicode_button_label));
    assert(api->publish(&activity));
    status = PumpUntilDrained(*api);
    assert(status.last_operation_succeeded == 1);
    {
        std::lock_guard<std::mutex> guard(normal.mutex);
        assert(
            normal.last_activity.find("\"type\":0") !=
            std::string::npos);
        assert(
            normal.last_activity.find(
                "\"status_display_type\":2") !=
            std::string::npos);
        assert(
            normal.last_activity.find(
                "\"timestamps\":{\"start\":1720000000}") !=
            std::string::npos);
        assert(
            normal.last_activity.find(
                "\"large_image\":\"https://steamuserimages-a."
                "akamaihd.net/example.jpg\"") !=
            std::string::npos);
        assert(
            normal.last_activity.find(
                "\"small_image\":\"dl_playing\"") !=
            std::string::npos);
        assert(
            normal.last_activity.find(
                "\"buttons\":[{\"label\":\"View on Steam Workshop\","
                "\"url\":\"https://steamcommunity.com/sharedfiles/"
                "filedetails/?id=3771245504\"}]") !=
            std::string::npos);
    }
    assert(api->clear());
    status = PumpUntilDrained(*api);
    assert(status.initialized == 0);
    assert(status.connected == 0);
    assert(
        InterlockedCompareExchange(
            &normal.pong_count, 0, 0) == 1);
    assert(
        InterlockedCompareExchange(
            &normal.clear_count, 0, 0) == 1);
    StopServer(normal);

    TestServer rejected;
    StartServer(rejected, ServerMode::RejectPublish);
    assert(api->initialize(1531147761955766444ULL));
    assert(api->publish(&activity));
    status = PumpUntilDrained(*api);
    assert(status.last_operation_succeeded == 0);
    assert(api->clear());
    status = PumpUntilDrained(*api);
    assert(status.last_operation_succeeded == 1);
    StopServer(rejected);

    TestServer oversized;
    StartServer(oversized, ServerMode::OversizedReady);
    assert(!api->initialize(1531147761955766444ULL));
    status = ReadStatus(*api);
    assert(status.initialized == 0);
    assert(status.connected == 0);
    StopServer(oversized);

    TestServer held;
    StartServer(held, ServerMode::HoldReplies);
    assert(api->initialize(1531147761955766444ULL));
    for (unsigned int index = 0; index < 7; ++index)
        assert(api->publish(&activity));
    assert(!api->publish(&activity));
    assert(!api->quiesce(0));
    status = ReadStatus(*api);
    assert(status.responses_pending == 8);
    assert(!api->initialize(1531147761955766444ULL));
    SetEvent(held.release_event);
    status = PumpUntilDrained(*api);
    assert(status.initialized == 0);
    assert(status.connected == 0);
    StopServer(held);

    TestServer reinitialized;
    StartServer(reinitialized, ServerMode::Normal);
    assert(api->initialize(1531147761955766444ULL));
    assert(api->quiesce(1000));
    StopServer(reinitialized);

    std::cout << "discord_presence_helper_fake_harness: ok\n";
    return 0;
}
