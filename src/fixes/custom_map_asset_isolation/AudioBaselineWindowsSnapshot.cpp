#define _HAS_EXCEPTIONS 0

#include "AudioBaselineWindowsSnapshot.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint32_t kBankHashMultiplier = 0x29U;

struct StringHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

struct VectorHeader {
    std::uintptr_t data;
    std::uint32_t size;
    std::uint32_t capacity;
};

struct BankTreeFrame {
    std::uintptr_t link;
    std::uintptr_t right;
    unsigned char body[kWindowsAudioBankNodeReadableSize];
};

struct RawPass {
    std::uintptr_t audio_singleton;
    std::uintptr_t audio_vtable;
    std::uintptr_t bank_manager;
    std::uintptr_t bank_tree_root;
    std::uintptr_t speech_manager;
    std::uint64_t script_manifest_fingerprint;
    std::uint64_t bank_manifest_fingerprint;
    std::uint64_t speech_bank_manifest_fingerprint;
    std::uint64_t raw_snapshot_fingerprint;
    std::uint64_t complete_fields;
    AudioBaselineWindowsSettingsSnapshot settings;
    AudioBaselineWindowsScriptPath scripts[kWindowsAudioScriptCapacity];
    AudioBaselineWindowsBankRecord banks[kWindowsAudioBankCapacity];
    AudioBaselineWindowsSpeechBankRecord
        speech_banks[kWindowsSpeechBankCapacity];
    std::uint32_t script_count;
    std::uint32_t bank_count;
    std::uint32_t speech_bank_count;
    AudioBaselineWindowsBackendKind backend_kind;
};

bool AddAddress(std::uintptr_t base, std::size_t offset,
                std::uintptr_t* output) noexcept
{
    if (!output || !base ||
        base > static_cast<std::uintptr_t>(-1) - offset)
        return false;
    *output = base + offset;
    return true;
}

bool ValidContext(const AudioBaselineWindowsReadContext* context) noexcept
{
    return context && context->engine_base && context->read_memory &&
        context->read_fence && context->try_enter_audio_lock &&
        context->leave_audio_lock;
}

bool ReadMemory(const AudioBaselineWindowsReadContext& context,
                std::uintptr_t address, void* output,
                std::size_t size) noexcept
{
    return address && output && size &&
        address <= static_cast<std::uintptr_t>(-1) - (size - 1U) &&
        context.read_memory(context.callback_user, address, output, size);
}

bool ReadAt(const AudioBaselineWindowsReadContext& context,
            std::uintptr_t base, std::size_t offset, void* output,
            std::size_t size) noexcept
{
    std::uintptr_t address = 0;
    return AddAddress(base, offset, &address) &&
        ReadMemory(context, address, output, size);
}

template <typename T>
T BodyValue(const unsigned char* body, std::size_t offset) noexcept
{
    T value = {};
    std::memcpy(&value, body + offset, sizeof(value));
    return value;
}

void AddReason(std::uint64_t* mask,
               AudioBaselineWindowsSnapshotReason reason) noexcept
{
    if (mask) *mask |= static_cast<std::uint64_t>(reason);
}

void HashBytes(std::uint64_t* hash, const void* data,
               std::size_t size) noexcept
{
    const unsigned char* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        *hash ^= bytes[index];
        *hash *= kFnvPrime;
    }
}

void HashU8(std::uint64_t* hash, std::uint8_t value) noexcept
{
    HashBytes(hash, &value, sizeof(value));
}

void HashU16(std::uint64_t* hash, std::uint16_t value) noexcept
{
    unsigned char bytes[2] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU32(std::uint64_t* hash, std::uint32_t value) noexcept
{
    unsigned char bytes[4] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU64(std::uint64_t* hash, std::uint64_t value) noexcept
{
    unsigned char bytes[8] = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 32),
        static_cast<unsigned char>(value >> 40),
        static_cast<unsigned char>(value >> 48),
        static_cast<unsigned char>(value >> 56),
    };
    HashBytes(hash, bytes, sizeof(bytes));
}

std::uint64_t BeginDomainHash(const char* domain) noexcept
{
    std::uint64_t hash = kFnvOffset;
    while (domain && *domain) {
        HashBytes(&hash, domain, 1U);
        ++domain;
    }
    return hash;
}

std::uint64_t NonzeroHash(std::uint64_t hash) noexcept
{
    return hash ? hash : kFnvOffset;
}

bool ReadRva(const AudioBaselineWindowsReadContext& context,
             std::uintptr_t rva, void* output, std::size_t size,
             std::uint64_t* reason_mask) noexcept
{
    std::uintptr_t address = 0;
    if (!AddAddress(context.engine_base, rva, &address)) {
        AddReason(reason_mask, AudioWindowsReasonAddressOverflow);
        return false;
    }
    if (!ReadMemory(context, address, output, size)) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    return true;
}

bool ReadBoundedString(const AudioBaselineWindowsReadContext& context,
                       const StringHeader& header, char* output,
                       std::size_t output_capacity,
                       std::uint64_t* reason_mask,
                       AudioBaselineWindowsSnapshotReason invalid_reason)
    noexcept
{
    if (!output || !output_capacity || !header.data || !header.size ||
        header.size > header.capacity ||
        header.size >= output_capacity) {
        AddReason(reason_mask, invalid_reason);
        return false;
    }
    if (!ReadMemory(context, header.data, output,
                    static_cast<std::size_t>(header.size) + 1U)) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    for (std::uint32_t index = 0; index < header.size; ++index) {
        if (output[index] == '\0') {
            AddReason(reason_mask, invalid_reason);
            return false;
        }
    }
    if (output[header.size] != '\0') {
        AddReason(reason_mask, invalid_reason);
        return false;
    }
    return true;
}

bool ReadBoundedSettingsSelector(
    const AudioBaselineWindowsReadContext& context,
    const StringHeader& header, char* output,
    std::size_t output_capacity,
    std::uint64_t* reason_mask) noexcept
{
    if (!output || !output_capacity ||
        header.size > header.capacity ||
        header.capacity >
            kWindowsAudioSettingsGlobalSelectorAllocationLimit ||
        header.size >= output_capacity ||
        (header.capacity && !header.data)) {
        AddReason(reason_mask,
                  AudioWindowsReasonAudioSettingsStringInvalid);
        return false;
    }
    if (header.size == 0U) {
        output[0] = '\0';
        return true;
    }
    return ReadBoundedString(
        context, header, output, output_capacity, reason_mask,
        AudioWindowsReasonAudioSettingsStringInvalid);
}

std::uint32_t ComputeBankNameHash(const char* name,
                                  std::uint32_t length) noexcept
{
    std::uint32_t hash = 0;
    for (std::uint32_t index = 0; index < length; ++index) {
        const std::int32_t byte =
            static_cast<std::int8_t>(name[index]);
        hash = hash * kBankHashMultiplier +
            static_cast<std::uint32_t>(byte);
    }
    return hash;
}

bool CaptureSettings(const AudioBaselineWindowsReadContext& context,
                     RawPass* pass, std::uint64_t* reason_mask) noexcept
{
    std::uintptr_t game = 0;
    if (!ReadRva(context, kWindowsGameGlobalRva, &game, sizeof(game),
                 reason_mask))
        return false;
    if (!game) {
        AddReason(reason_mask, AudioWindowsReasonGameUnavailable);
        return false;
    }

    std::uintptr_t game_vtable = 0;
    std::uintptr_t expected_vtable = 0;
    if (!ReadMemory(context, game, &game_vtable, sizeof(game_vtable))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (!AddAddress(context.engine_base, kWindowsGameVtableRva,
                    &expected_vtable)) {
        AddReason(reason_mask, AudioWindowsReasonAddressOverflow);
        return false;
    }
    if (game_vtable != expected_vtable) {
        AddReason(reason_mask, AudioWindowsReasonGameIdentityMismatch);
        return false;
    }

    std::uintptr_t settings = 0;
    if (!ReadAt(context, game, kWindowsGameAudioSettingsOffset,
                &settings, sizeof(settings))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (!settings) {
        AddReason(reason_mask, AudioWindowsReasonAudioSettingsUnavailable);
        return false;
    }

    AudioBaselineWindowsSettingsSnapshot captured = {};
    captured.game = game;
    captured.settings = settings;
    if (!ReadMemory(context, settings, captured.pod,
                    sizeof(captured.pod)) ||
        !ReadRva(context, kWindowsAudioVolumeGrainRva,
                 &captured.volume_grain_bits,
                 sizeof(captured.volume_grain_bits), reason_mask)) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    captured.fx_volume_bits = BodyValue<std::uint32_t>(
        captured.pod, kWindowsAudioSettingsFxVolumeOffset);
    captured.wave_volume_bits = BodyValue<std::uint32_t>(
        captured.pod, kWindowsAudioSettingsWaveVolumeOffset);
    captured.speech_volume_bits = BodyValue<std::uint32_t>(
        captured.pod, kWindowsAudioSettingsSpeechVolumeOffset);
    captured.movie_volume_bits = BodyValue<std::uint32_t>(
        captured.pod, kWindowsAudioSettingsMovieVolumeOffset);
    captured.movie_volume_source_bits = BodyValue<std::uint32_t>(
        captured.pod,
        kWindowsAudioSettingsMovieVolumeSourceOffset);
    const StringHeader global_settings = BodyValue<StringHeader>(
        captured.pod, kWindowsAudioSettingsGlobalSelectorOffset);
    if (!ReadBoundedSettingsSelector(
            context, global_settings, captured.global_settings,
            sizeof(captured.global_settings), reason_mask))
        return false;
    captured.global_settings_length = global_settings.size;
    std::uint64_t selector_hash =
        BeginDomainHash("audio_global_settings_selector_v1");
    HashBytes(&selector_hash, captured.global_settings,
              captured.global_settings_length);
    captured.global_settings_fingerprint =
        NonzeroHash(selector_hash);

    /* The raw POD is retained for exact in-process evidence and double-pass
     * stability.  Its semantic fingerprint deliberately excludes the TTL
     * pointer/capacity and padding, then includes the bounded string bytes. */
    std::uint64_t hash =
        BeginDomainHash("audio_settings_semantic_v1");
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x00U));
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x04U));
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x08U));
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x0CU));
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x10U));
    HashU32(&hash, BodyValue<std::uint32_t>(captured.pod, 0x14U));
    HashU16(&hash, BodyValue<std::uint16_t>(captured.pod, 0x18U));
    for (std::size_t offset = 0x1CU; offset <= 0x44U;
         offset += sizeof(std::uint32_t))
        HashU32(&hash,
                BodyValue<std::uint32_t>(captured.pod, offset));
    HashU32(&hash, captured.volume_grain_bits);
    HashU32(&hash, captured.global_settings_length);
    HashBytes(&hash, captured.global_settings,
              captured.global_settings_length);
    captured.fingerprint = NonzeroHash(hash);
    captured.available = true;
    pass->settings = captured;
    pass->complete_fields |= AudioWindowsFieldAudioSettings;
    return true;
}

bool CaptureScriptPaths(const AudioBaselineWindowsReadContext& context,
                        RawPass* pass,
                        std::uint64_t* reason_mask) noexcept
{
    VectorHeader header = {};
    if (!ReadRva(context, kWindowsAudioScriptPathVectorRva,
                 &header, sizeof(header), reason_mask))
        return false;
    if (header.size > header.capacity) {
        AddReason(reason_mask, AudioWindowsReasonScriptHeaderInvalid);
        return false;
    }
    if (header.size > kWindowsAudioScriptCapacity ||
        header.capacity > kWindowsAudioScriptAllocationLimit) {
        AddReason(reason_mask, AudioWindowsReasonScriptCountOutOfRange);
        return false;
    }
    if (header.capacity && !header.data) {
        AddReason(reason_mask, AudioWindowsReasonScriptHeaderInvalid);
        return false;
    }

    std::uint64_t manifest =
        BeginDomainHash("audio_script_path_manifest_v1");
    HashU32(&manifest, header.size);
    for (std::uint32_t index = 0; index < header.size; ++index) {
        std::uintptr_t address = 0;
        if (!AddAddress(header.data,
                        static_cast<std::size_t>(index) *
                            kWindowsAudioScriptStringStride,
                        &address)) {
            AddReason(reason_mask, AudioWindowsReasonAddressOverflow);
            return false;
        }
        StringHeader script = {};
        if (!ReadMemory(context, address, &script, sizeof(script))) {
            AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
            return false;
        }
        AudioBaselineWindowsScriptPath& captured = pass->scripts[index];
        if (!ReadBoundedString(context, script, captured.logical_path,
                               sizeof(captured.logical_path), reason_mask,
                               AudioWindowsReasonScriptPathInvalid))
            return false;
        captured.load_order = index;
        captured.length = script.size;
        std::uint64_t path_hash =
            BeginDomainHash("audio_script_logical_path_v1");
        HashBytes(&path_hash, captured.logical_path, captured.length);
        captured.logical_path_fingerprint = NonzeroHash(path_hash);
        HashU32(&manifest, captured.load_order);
        HashU32(&manifest, captured.length);
        HashU64(&manifest, captured.logical_path_fingerprint);
    }
    pass->script_count = header.size;
    pass->script_manifest_fingerprint = NonzeroHash(manifest);
    pass->complete_fields |= AudioWindowsFieldAudioScriptPaths;
    return true;
}

bool EqualVectorHeader(const VectorHeader& left,
                       const VectorHeader& right) noexcept
{
    return left.data == right.data && left.size == right.size &&
        left.capacity == right.capacity;
}

bool CaptureSpeechBanks(const AudioBaselineWindowsReadContext& context,
                        RawPass* pass,
                        std::uint64_t* reason_mask) noexcept
{
    if (!ReadRva(context, kWindowsSpeechManagerGlobalRva,
                 &pass->speech_manager,
                 sizeof(pass->speech_manager), reason_mask))
        return false;
    if (!pass->speech_manager) {
        AddReason(reason_mask,
                  AudioWindowsReasonSpeechManagerUnavailable);
        return false;
    }
    pass->complete_fields |= AudioWindowsFieldSpeechManager;

    VectorHeader before = {};
    std::uint32_t tag_before = 0;
    if (!ReadAt(context, pass->speech_manager,
                kWindowsSpeechBankVectorOffset, &before,
                sizeof(before)) ||
        !ReadAt(context, pass->speech_manager,
                kWindowsSpeechBankVectorTagOffset, &tag_before,
                sizeof(tag_before))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (tag_before != kWindowsSpeechBankVectorTag) {
        AddReason(reason_mask, AudioWindowsReasonSpeechHeaderInvalid);
        return false;
    }
    if (before.size > before.capacity) {
        AddReason(reason_mask, AudioWindowsReasonSpeechHeaderInvalid);
        return false;
    }
    if (before.size > kWindowsSpeechBankCapacity ||
        before.capacity > kWindowsSpeechBankCapacity) {
        AddReason(reason_mask, AudioWindowsReasonSpeechCountOutOfRange);
        return false;
    }
    if (before.capacity && !before.data) {
        AddReason(reason_mask, AudioWindowsReasonSpeechHeaderInvalid);
        return false;
    }

    std::uintptr_t first_pointers[kWindowsSpeechBankCapacity] = {};
    std::uintptr_t second_pointers[kWindowsSpeechBankCapacity] = {};
    std::uint64_t manifest =
        BeginDomainHash("speech_bank_vector_observation_v1");
    HashU32(&manifest, before.size);
    for (std::uint32_t index = 0; index < before.size; ++index) {
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(std::uintptr_t);
        if (!ReadAt(context, before.data, offset,
                    &first_pointers[index],
                    sizeof(first_pointers[index]))) {
            AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
            return false;
        }
        if (!first_pointers[index]) {
            AddReason(reason_mask,
                      AudioWindowsReasonSpeechRecordInvalid);
            return false;
        }
        for (std::uint32_t prior = 0; prior < index; ++prior) {
            if (first_pointers[prior] == first_pointers[index]) {
                AddReason(reason_mask,
                          AudioWindowsReasonSpeechRecordInvalid);
                return false;
            }
        }

        AudioBaselineWindowsSpeechBankRecord& record =
            pass->speech_banks[index];
        record.bank = first_pointers[index];
        if (!ReadMemory(context, record.bank, record.descriptor,
                        sizeof(record.descriptor))) {
            AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
            return false;
        }
        record.bank_hash = BodyValue<std::uint32_t>(
            record.descriptor, kWindowsSpeechBankHashOffset);
        const std::uint8_t localised = BodyValue<std::uint8_t>(
            record.descriptor, kWindowsSpeechBankLocalisedOffset);
        if (localised > 1U) {
            AddReason(reason_mask,
                      AudioWindowsReasonSpeechRecordInvalid);
            return false;
        }
        record.localised = localised != 0;
        std::uint64_t descriptor_hash =
            BeginDomainHash("speech_bank_descriptor_raw_v1");
        HashBytes(&descriptor_hash, record.descriptor,
                  sizeof(record.descriptor));
        record.descriptor_fingerprint =
            NonzeroHash(descriptor_hash);
        HashU32(&manifest, index);
        HashU32(&manifest, record.bank_hash);
        HashU8(&manifest, localised);
        HashU64(&manifest, record.descriptor_fingerprint);
    }

    VectorHeader after = {};
    std::uint32_t tag_after = 0;
    if (!ReadAt(context, pass->speech_manager,
                kWindowsSpeechBankVectorOffset, &after,
                sizeof(after)) ||
        !ReadAt(context, pass->speech_manager,
                kWindowsSpeechBankVectorTagOffset, &tag_after,
                sizeof(tag_after))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (!EqualVectorHeader(before, after) ||
        tag_after != tag_before) {
        AddReason(reason_mask, AudioWindowsReasonSpeechVectorChanged);
        return false;
    }
    for (std::uint32_t index = 0; index < before.size; ++index) {
        const std::size_t offset =
            static_cast<std::size_t>(index) * sizeof(std::uintptr_t);
        if (!ReadAt(context, after.data, offset,
                    &second_pointers[index],
                    sizeof(second_pointers[index]))) {
            AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
            return false;
        }
        if (second_pointers[index] != first_pointers[index]) {
            AddReason(reason_mask, AudioWindowsReasonSpeechVectorChanged);
            return false;
        }
    }

    pass->speech_bank_count = before.size;
    pass->speech_bank_manifest_fingerprint = NonzeroHash(manifest);
    pass->complete_fields |=
        AudioWindowsFieldSpeechBankVectorObserved;
    return true;
}

bool SeenLink(const std::uintptr_t* visited, std::uint32_t count,
              std::uintptr_t link) noexcept
{
    for (std::uint32_t index = 0; index < count; ++index)
        if (visited[index] == link) return true;
    return false;
}

bool CaptureBankRecord(const AudioBaselineWindowsReadContext& context,
                       const BankTreeFrame& frame,
                       bool root,
                       AudioBaselineWindowsBankRecord* output,
                       std::uint64_t* reason_mask) noexcept
{
    if (!output || frame.link < kWindowsAudioBankNodeLinkOffset) {
        AddReason(reason_mask, AudioWindowsReasonBankNodeInvalid);
        return false;
    }
    const std::uintptr_t node =
        frame.link - kWindowsAudioBankNodeLinkOffset;
    const StringHeader name = BodyValue<StringHeader>(
        frame.body, kWindowsAudioBankNodeNameOffset);
    if (!ReadBoundedString(context, name, output->name,
                           sizeof(output->name), reason_mask,
                           AudioWindowsReasonBankNameInvalid))
        return false;

    output->node_link = frame.link;
    output->record = node;
    output->native_file = BodyValue<std::uintptr_t>(
        frame.body, kWindowsAudioBankNodeNativeFileOffset);
    output->tree_key = BodyValue<std::uint32_t>(
        frame.body, kWindowsAudioBankNodeHashOffset);
    output->record_hash = BodyValue<std::uint32_t>(
        frame.body, kWindowsAudioBankNodeRecordHashOffset);
    output->name_length = name.size;
    output->tree_color = BodyValue<std::uint32_t>(
        frame.body, kWindowsAudioBankNodeColorOffset);
    const std::uint8_t localised = BodyValue<std::uint8_t>(
        frame.body, kWindowsAudioBankNodeLocalisedOffset);
    if (!output->native_file || localised > 1U ||
        output->tree_color > 1U || (root && output->tree_color != 1U)) {
        AddReason(reason_mask, AudioWindowsReasonBankNodeInvalid);
        return false;
    }
    output->localised = localised != 0;
    if (ComputeBankNameHash(output->name, output->name_length) !=
        output->tree_key) {
        AddReason(reason_mask, AudioWindowsReasonBankHashMismatch);
        return false;
    }

    std::uint64_t hash = BeginDomainHash("audio_bank_record_raw_v1");
    HashU32(&hash, output->tree_key);
    HashU32(&hash, output->record_hash);
    HashU32(&hash, output->name_length);
    HashBytes(&hash, output->name, output->name_length);
    HashU8(&hash, localised);
    HashU32(&hash, output->tree_color);
    output->structural_fingerprint = NonzeroHash(hash);
    return true;
}

bool CaptureFmodBanks(const AudioBaselineWindowsReadContext& context,
                      RawPass* pass, std::uint64_t* reason_mask) noexcept
{
    if (!ReadAt(context, pass->audio_singleton,
                kWindowsAudioFmodBankManagerOffset,
                &pass->bank_manager, sizeof(pass->bank_manager))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (!pass->bank_manager) {
        AddReason(reason_mask, AudioWindowsReasonBankManagerUnavailable);
        return false;
    }
    pass->complete_fields |= AudioWindowsFieldBankManager;

    std::uint32_t count = 0;
    if (!ReadAt(context, pass->bank_manager,
                kWindowsAudioBankTreeRootOffset,
                &pass->bank_tree_root,
                sizeof(pass->bank_tree_root)) ||
        !ReadAt(context, pass->bank_manager,
                kWindowsAudioBankTreeCountOffset,
                &count, sizeof(count))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }
    if (count > kWindowsAudioBankCapacity) {
        AddReason(reason_mask, AudioWindowsReasonBankCountOutOfRange);
        return false;
    }
    if ((!pass->bank_tree_root) != (count == 0U)) {
        AddReason(reason_mask, AudioWindowsReasonBankHeaderInvalid);
        return false;
    }

    BankTreeFrame stack[kWindowsAudioBankCapacity] = {};
    std::uintptr_t visited[kWindowsAudioBankCapacity] = {};
    std::uint32_t depth = 0;
    std::uint32_t visited_count = 0;
    std::uint32_t output_count = 0;
    std::uint32_t previous_key = 0;
    bool have_previous_key = false;
    std::uintptr_t current = pass->bank_tree_root;
    std::uintptr_t expected_parent = 0;
    std::uint64_t manifest = BeginDomainHash("audio_bank_manifest_raw_v1");
    HashU32(&manifest, count);

    while (current || depth) {
        while (current) {
            if (SeenLink(visited, visited_count, current)) {
                AddReason(reason_mask, AudioWindowsReasonBankTreeCycle);
                return false;
            }
            if (visited_count >= count) {
                AddReason(reason_mask, AudioWindowsReasonBankCountMismatch);
                return false;
            }
            if (current < kWindowsAudioBankNodeLinkOffset) {
                AddReason(reason_mask, AudioWindowsReasonBankNodeInvalid);
                return false;
            }
            const std::uintptr_t node =
                current - kWindowsAudioBankNodeLinkOffset;
            BankTreeFrame frame = {};
            frame.link = current;
            if (!ReadMemory(context, node, frame.body,
                            sizeof(frame.body))) {
                AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
                return false;
            }
            const std::uintptr_t parent = BodyValue<std::uintptr_t>(
                frame.body, kWindowsAudioBankNodeParentOffset);
            if (parent != expected_parent) {
                AddReason(reason_mask, AudioWindowsReasonBankNodeInvalid);
                return false;
            }
            frame.right = BodyValue<std::uintptr_t>(
                frame.body, kWindowsAudioBankNodeRightOffset);
            if (depth >= kWindowsAudioBankCapacity) {
                AddReason(reason_mask, AudioWindowsReasonBankCountOutOfRange);
                return false;
            }
            visited[visited_count++] = current;
            stack[depth++] = frame;
            expected_parent = current;
            current = BodyValue<std::uintptr_t>(
                frame.body, kWindowsAudioBankNodeLeftOffset);
        }

        const BankTreeFrame frame = stack[--depth];
        AudioBaselineWindowsBankRecord& record =
            pass->banks[output_count];
        if (!CaptureBankRecord(
                context, frame,
                frame.link == pass->bank_tree_root,
                &record, reason_mask))
            return false;
        if (have_previous_key && record.tree_key <= previous_key) {
            AddReason(reason_mask, AudioWindowsReasonBankNodeInvalid);
            return false;
        }
        previous_key = record.tree_key;
        have_previous_key = true;
        HashU32(&manifest, output_count);
        HashU64(&manifest, record.structural_fingerprint);
        ++output_count;
        expected_parent = frame.link;
        current = frame.right;
    }

    if (visited_count != count || output_count != count) {
        AddReason(reason_mask, AudioWindowsReasonBankCountMismatch);
        return false;
    }
    pass->bank_count = count;
    pass->bank_manifest_fingerprint = NonzeroHash(manifest);
    pass->complete_fields |= AudioWindowsFieldBankTree;
    return true;
}

bool CaptureBackendAndBanks(
    const AudioBaselineWindowsReadContext& context, RawPass* pass,
    std::uint64_t* reason_mask) noexcept
{
    if (!ReadRva(context, kWindowsAudioSingletonGlobalRva,
                 &pass->audio_singleton,
                 sizeof(pass->audio_singleton), reason_mask))
        return false;
    if (!pass->audio_singleton) {
        AddReason(reason_mask, AudioWindowsReasonAudioSingletonUnavailable);
        return false;
    }
    pass->complete_fields |= AudioWindowsFieldAudioSingleton;
    if (!ReadMemory(context, pass->audio_singleton,
                    &pass->audio_vtable,
                    sizeof(pass->audio_vtable))) {
        AddReason(reason_mask, AudioWindowsReasonMemoryUnreadable);
        return false;
    }

    std::uintptr_t fmod_vtable = 0;
    std::uintptr_t no_sound_vtable = 0;
    if (!AddAddress(context.engine_base, kWindowsAudioFmodVtableRva,
                    &fmod_vtable) ||
        !AddAddress(context.engine_base, kWindowsAudioNoSoundVtableRva,
                    &no_sound_vtable)) {
        AddReason(reason_mask, AudioWindowsReasonAddressOverflow);
        return false;
    }
    if (pass->audio_vtable == fmod_vtable)
        pass->backend_kind = AudioBaselineWindowsBackendKind::Fmod;
    else if (pass->audio_vtable == no_sound_vtable)
        pass->backend_kind = AudioBaselineWindowsBackendKind::NoSound;
    else {
        AddReason(reason_mask, AudioWindowsReasonBackendIdentityMismatch);
        return false;
    }
    pass->complete_fields |= AudioWindowsFieldBackendIdentity;

    if (pass->backend_kind == AudioBaselineWindowsBackendKind::Fmod)
        return CaptureFmodBanks(context, pass, reason_mask);

    pass->bank_manager = 0;
    pass->bank_tree_root = 0;
    pass->bank_count = 0;
    std::uint64_t manifest = BeginDomainHash("audio_bank_manifest_raw_v1");
    HashU32(&manifest, 0);
    pass->bank_manifest_fingerprint = NonzeroHash(manifest);
    pass->complete_fields |= AudioWindowsFieldBankManager;
    pass->complete_fields |= AudioWindowsFieldBankTree;
    return true;
}

void FinalizeRawFingerprint(RawPass* pass) noexcept
{
    std::uint64_t hash = BeginDomainHash("audio_windows_raw_snapshot_v1");
    HashU8(&hash, static_cast<std::uint8_t>(pass->backend_kind));
    HashU64(&hash, pass->settings.fingerprint);
    HashU64(&hash, pass->script_manifest_fingerprint);
    HashU64(&hash, pass->bank_manifest_fingerprint);
    HashU64(&hash, pass->speech_bank_manifest_fingerprint);
    pass->raw_snapshot_fingerprint = NonzeroHash(hash);
}

bool CaptureRawPass(const AudioBaselineWindowsReadContext& context,
                    bool capture_speech, RawPass* pass,
                    std::uint64_t* reason_mask) noexcept
{
    if (!pass) return false;
    std::memset(pass, 0, sizeof(*pass));
    if (!CaptureBackendAndBanks(context, pass, reason_mask) ||
        !CaptureSettings(context, pass, reason_mask) ||
        !CaptureScriptPaths(context, pass, reason_mask))
        return false;
    /* The validated FMOD lock is not evidence for SpeechMgr ownership.  Do
     * not even read the speech-manager global unless the caller supplies a
     * separately proven synchronization contract. */
    if (capture_speech &&
        !CaptureSpeechBanks(context, pass, reason_mask))
        return false;
    FinalizeRawFingerprint(pass);
    return true;
}

bool EqualRawPassToSnapshot(
    const RawPass& pass, const AudioBaselineWindowsSnapshot& snapshot,
    std::uint64_t expected_complete_fields) noexcept
{
    return pass.audio_singleton == snapshot.audio_singleton &&
        pass.audio_vtable == snapshot.audio_vtable &&
        pass.bank_manager == snapshot.bank_manager &&
        pass.bank_tree_root == snapshot.bank_tree_root &&
        pass.speech_manager == snapshot.speech_manager &&
        pass.script_manifest_fingerprint ==
            snapshot.script_manifest_fingerprint &&
        pass.bank_manifest_fingerprint ==
            snapshot.bank_manifest_fingerprint &&
        pass.speech_bank_manifest_fingerprint ==
            snapshot.speech_bank_manifest_fingerprint &&
        pass.raw_snapshot_fingerprint ==
            snapshot.raw_snapshot_fingerprint &&
        pass.complete_fields == expected_complete_fields &&
        pass.script_count == snapshot.script_count &&
        pass.bank_count == snapshot.bank_count &&
        pass.speech_bank_count == snapshot.speech_bank_count &&
        pass.backend_kind == snapshot.backend_kind &&
        std::memcmp(&pass.settings, &snapshot.settings,
                    sizeof(pass.settings)) == 0 &&
        std::memcmp(pass.scripts, snapshot.scripts,
                    sizeof(pass.scripts)) == 0 &&
        std::memcmp(pass.banks, snapshot.banks,
                    sizeof(pass.banks)) == 0 &&
        std::memcmp(pass.speech_banks, snapshot.speech_banks,
                    sizeof(pass.speech_banks)) == 0;
}

/* Reuses the caller's sole RawPass workspace.  The first pass has already
 * been copied into the output snapshot, so a second simultaneous ~110 KiB
 * object is unnecessary.  Unproven speech ownership still incurs neither
 * speech reads nor this second pass. */
bool VerifySpeechSynchronizedSecondPass(
    const AudioBaselineWindowsReadContext& context,
    const AudioBaselineWindowsSnapshot& first,
    std::uint64_t first_complete_fields, RawPass* workspace,
    std::uint64_t* reason_mask) noexcept
{
    if (!workspace ||
        !CaptureRawPass(context, true, workspace, reason_mask))
        return false;
    if (!EqualRawPassToSnapshot(
            *workspace, first, first_complete_fields)) {
        AddReason(reason_mask, AudioWindowsReasonSnapshotChanged);
        return false;
    }
    return true;
}

void CopyRawPass(const RawPass& source,
                 AudioBaselineWindowsSnapshot* output) noexcept
{
    output->audio_singleton = source.audio_singleton;
    output->audio_vtable = source.audio_vtable;
    output->bank_manager = source.bank_manager;
    output->bank_tree_root = source.bank_tree_root;
    output->speech_manager = source.speech_manager;
    output->script_manifest_fingerprint =
        source.script_manifest_fingerprint;
    output->bank_manifest_fingerprint =
        source.bank_manifest_fingerprint;
    output->speech_bank_manifest_fingerprint =
        source.speech_bank_manifest_fingerprint;
    output->raw_snapshot_fingerprint = source.raw_snapshot_fingerprint;
    output->settings = source.settings;
    std::memcpy(output->scripts, source.scripts,
                sizeof(output->scripts));
    std::memcpy(output->banks, source.banks,
                sizeof(output->banks));
    std::memcpy(output->speech_banks, source.speech_banks,
                sizeof(output->speech_banks));
    output->script_count = source.script_count;
    output->bank_count = source.bank_count;
    output->speech_bank_count = source.speech_bank_count;
    output->backend_kind = source.backend_kind;
}

}  // namespace

bool SnapshotAudioBaselineWindows(
    const AudioBaselineWindowsReadContext* context,
    AudioBaselineWindowsSnapshot* output) noexcept
{
    if (!output) return false;
    std::memset(output, 0, sizeof(*output));
    output->unresolved_fields =
        AudioWindowsUnresolvedScriptProviders |
        AudioWindowsUnresolvedBankProviders |
        AudioWindowsUnresolvedBankContent |
        AudioWindowsUnresolvedBankDependencyOrder |
        AudioWindowsUnresolvedBankReferenceModel |
        AudioWindowsUnresolvedBankInstanceGeneration |
        AudioWindowsUnresolvedSpeechSynchronization |
        AudioWindowsUnresolvedSpeechProviders |
        AudioWindowsUnresolvedSpeechContent |
        AudioWindowsUnresolvedSpeechDependencyOrder |
        AudioWindowsUnresolvedSpeechReferenceModel |
        AudioWindowsUnresolvedSpeechInstanceGeneration |
        AudioWindowsUnresolvedAudioSettingsProvider;

    if (!ValidContext(context)) {
        AddReason(&output->reason_mask,
                  AudioWindowsReasonInvalidArgument);
        return false;
    }
    if (!context->exact_engine_image_validated) {
        AddReason(&output->reason_mask,
                  AudioWindowsReasonUnsupportedImage);
        return false;
    }
    output->complete_fields |= AudioWindowsFieldExactImage;

    AudioBaselineWindowsObservationFence before = {};
    if (!context->read_fence(context->callback_user, &before)) {
        AddReason(&output->reason_mask,
                  AudioWindowsReasonFenceUnavailable);
        return false;
    }
    output->observation_revision = before.revision;
    if (before.active_calls != 0U) {
        AddReason(&output->reason_mask, AudioWindowsReasonObserverBusy);
        return false;
    }
    if (before.observation_loss) {
        AddReason(&output->reason_mask, AudioWindowsReasonObservationLoss);
        return false;
    }

    if (!context->try_enter_audio_lock(context->callback_user)) {
        AddReason(&output->reason_mask, AudioWindowsReasonAudioLockBusy);
        return false;
    }
    output->lock_acquired = true;
    output->complete_fields |= AudioWindowsFieldAudioLock;

    const bool capture_speech =
        context->speech_registry_synchronization_proven;
    RawPass workspace = {};
    const bool first_ok =
        CaptureRawPass(*context, capture_speech, &workspace,
                       &output->reason_mask);
    const std::uint64_t first_complete_fields =
        workspace.complete_fields;
    if (first_ok) CopyRawPass(workspace, output);
    const bool second_ok = first_ok &&
        (!capture_speech || VerifySpeechSynchronizedSecondPass(
            *context, *output, first_complete_fields, &workspace,
            &output->reason_mask));
    context->leave_audio_lock(context->callback_user);
    if (!first_ok || !second_ok) return false;

    AudioBaselineWindowsObservationFence after = {};
    if (!context->read_fence(context->callback_user, &after)) {
        AddReason(&output->reason_mask,
                  AudioWindowsReasonFenceUnavailable);
        return false;
    }
    if (after.active_calls != 0U) {
        AddReason(&output->reason_mask, AudioWindowsReasonObserverBusy);
        return false;
    }
    if (after.observation_loss) {
        AddReason(&output->reason_mask, AudioWindowsReasonObservationLoss);
        return false;
    }
    if (after.revision != before.revision) {
        AddReason(&output->reason_mask, AudioWindowsReasonFenceChanged);
        return false;
    }

    output->complete_fields |= first_complete_fields;
    output->complete_fields |= AudioWindowsFieldObserverFence;
    output->sound_lock_snapshot_complete =
        (output->complete_fields &
         kAudioWindowsRequiredSoundLockFields) ==
            kAudioWindowsRequiredSoundLockFields;
    output->raw_snapshot_complete =
        (output->complete_fields & kAudioWindowsRequiredRawFields) ==
            kAudioWindowsRequiredRawFields;
    output->speech_vector_observation_stable =
        (output->complete_fields &
         AudioWindowsFieldSpeechBankVectorObserved) != 0;
    output->speech_owner_synchronization_proven = capture_speech &&
        output->speech_vector_observation_stable;
    if (output->speech_owner_synchronization_proven)
        output->unresolved_fields &=
            ~static_cast<std::uint64_t>(
                AudioWindowsUnresolvedSpeechSynchronization);
    output->authoritative_b0_complete = output->raw_snapshot_complete &&
        output->unresolved_fields == 0;
    /* Synchronization plus raw membership is still not provider/content/
     * order/generation evidence, so it cannot complete Speech B0 alone. */
    output->speech_baseline_complete = false;
    return output->sound_lock_snapshot_complete &&
        (!capture_speech || output->raw_snapshot_complete);
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot
