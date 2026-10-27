#include "fixes/custom_map_asset_isolation/AudioBaselineWindowsSnapshot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace audio =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::audio_snapshot;

namespace {

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,  \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uintptr_t kEngineBase = 0x0000000180000000ULL;
constexpr std::uintptr_t kAudio = kEngineBase + 0x00D00000U;
constexpr std::uintptr_t kGame = kEngineBase + 0x00D02000U;
constexpr std::uintptr_t kSettings = kEngineBase + 0x00D03000U;
constexpr std::uintptr_t kBankManager = kEngineBase + 0x00D04000U;
constexpr std::uintptr_t kBankNode = kEngineBase + 0x00D05000U;
constexpr std::uintptr_t kBankNode2 = kEngineBase + 0x00D05100U;
constexpr std::uintptr_t kBankName = kEngineBase + 0x00D06000U;
constexpr std::uintptr_t kBankName2 = kEngineBase + 0x00D06200U;
constexpr std::uintptr_t kNativeFile = kEngineBase + 0x00D06800U;
constexpr std::uintptr_t kScriptHeaders = kEngineBase + 0x00D07000U;
constexpr std::uintptr_t kScriptPath = kEngineBase + 0x00D07400U;
constexpr std::uintptr_t kGlobalSettings = kEngineBase + 0x00D07800U;
constexpr std::uintptr_t kAlternateGlobalSettings =
    kEngineBase + 0x00D07A00U;
constexpr std::uintptr_t kSpeechManager = kEngineBase + 0x00D08000U;
constexpr std::uintptr_t kSpeechPointers = kEngineBase + 0x00D08100U;
constexpr std::uintptr_t kSpeechBank = kEngineBase + 0x00D08200U;
constexpr std::uintptr_t kAlternateSpeechBank =
    kEngineBase + 0x00D08300U;

unsigned char g_image[audio::kWindowsAudioEngineSizeOfImage];

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

struct FakeContext {
    std::uint64_t revision;
    std::uint32_t active_calls;
    std::uintptr_t unreadable_address;
    std::uint32_t fence_reads;
    std::uint32_t lock_attempts;
    std::uint32_t lock_leaves;
    std::uint32_t settings_pod_reads;
    std::uint32_t settings_reads;
    std::uint32_t speech_pointer_reads;
    std::uint32_t speech_registry_reads;
    bool observation_loss;
    bool lock_available;
    bool change_fence_revision;
    bool change_settings_between_passes;
    bool change_speech_pointer_within_pass;
};

std::size_t ImageOffset(std::uintptr_t address) noexcept
{
    return static_cast<std::size_t>(address - kEngineBase);
}

template <typename T>
void Store(std::uintptr_t address, T value) noexcept
{
    std::memcpy(g_image + ImageOffset(address), &value, sizeof(value));
}

void StoreBytes(std::uintptr_t address, const void* data,
                std::size_t size) noexcept
{
    std::memcpy(g_image + ImageOffset(address), data, size);
}

void StoreCString(std::uintptr_t address, const char* value) noexcept
{
    StoreBytes(address, value, std::strlen(value) + 1U);
}

std::uint32_t BankNameHash(const char* name) noexcept
{
    std::uint32_t hash = 0;
    while (*name) {
        const std::int32_t byte = static_cast<std::int8_t>(*name++);
        hash = hash * 0x29U + static_cast<std::uint32_t>(byte);
    }
    return hash;
}

bool Intersects(std::uintptr_t address, std::size_t size,
                std::uintptr_t target) noexcept
{
    return target && address <= target &&
        target - address < size;
}

bool ReadFakeMemory(void* user, std::uintptr_t address, void* output,
                    std::size_t size) noexcept
{
    FakeContext* state = static_cast<FakeContext*>(user);
    if (!state || !output || !size || address < kEngineBase ||
        address > static_cast<std::uintptr_t>(-1) - size ||
        address + size >
            kEngineBase + audio::kWindowsAudioEngineSizeOfImage ||
        Intersects(address, size, state->unreadable_address))
        return false;

    if (Intersects(address, size,
                   kEngineBase +
                       audio::kWindowsSpeechManagerGlobalRva) ||
        Intersects(address, size,
                   kSpeechManager +
                       audio::kWindowsSpeechBankVectorOffset) ||
        Intersects(address, size,
                   kSpeechManager +
                       audio::kWindowsSpeechBankVectorTagOffset) ||
        Intersects(address, size, kSpeechPointers) ||
        Intersects(address, size, kSpeechBank) ||
        Intersects(address, size, kAlternateSpeechBank))
        ++state->speech_registry_reads;

    if (address == kSettings &&
        size == audio::kWindowsAudioSettingsPodSize) {
        ++state->settings_pod_reads;
    }
    if (state->change_settings_between_passes &&
        address == kSettings &&
        size == audio::kWindowsAudioSettingsPodSize) {
        std::memcpy(output, g_image + ImageOffset(address), size);
        if (state->settings_reads++ != 0U) {
            unsigned char* bytes = static_cast<unsigned char*>(output);
            ++bytes[audio::kWindowsAudioSettingsFxVolumeOffset];
        }
        return true;
    }
    if (state->change_speech_pointer_within_pass &&
        address == kSpeechPointers &&
        size == sizeof(std::uintptr_t)) {
        const std::uintptr_t value =
            state->speech_pointer_reads++ == 0U ?
                kSpeechBank : kAlternateSpeechBank;
        std::memcpy(output, &value, sizeof(value));
        return true;
    }

    std::memcpy(output, g_image + ImageOffset(address), size);
    return true;
}

bool ReadFakeFence(
    void* user,
    audio::AudioBaselineWindowsObservationFence* output) noexcept
{
    FakeContext* state = static_cast<FakeContext*>(user);
    if (!state || !output) return false;
    output->revision = state->revision;
    if (state->change_fence_revision && state->fence_reads != 0U)
        ++output->revision;
    output->active_calls = state->active_calls;
    output->observation_loss = state->observation_loss;
    ++state->fence_reads;
    return true;
}

bool TryFakeLock(void* user) noexcept
{
    FakeContext* state = static_cast<FakeContext*>(user);
    if (!state) return false;
    ++state->lock_attempts;
    return state->lock_available;
}

void LeaveFakeLock(void* user) noexcept
{
    FakeContext* state = static_cast<FakeContext*>(user);
    if (state) ++state->lock_leaves;
}

void InitializeBankNode(std::uintptr_t node, const char* name,
                        std::uintptr_t name_address,
                        std::uintptr_t parent_link,
                        std::uintptr_t left_link,
                        std::uintptr_t right_link,
                        std::uint32_t record_hash,
                        bool localised) noexcept
{
    const std::uint32_t length =
        static_cast<std::uint32_t>(std::strlen(name));
    const StringHeader text = {name_address, length, length};
    Store(node + audio::kWindowsAudioBankNodeHashOffset,
          BankNameHash(name));
    Store(node + audio::kWindowsAudioBankNodeNameOffset, text);
    Store(node + audio::kWindowsAudioBankNodeNativeFileOffset,
          kNativeFile);
    Store(node + audio::kWindowsAudioBankNodeRecordHashOffset,
          record_hash);
    Store(node + audio::kWindowsAudioBankNodeLocalisedOffset,
          static_cast<std::uint8_t>(localised ? 1U : 0U));
    Store(node + audio::kWindowsAudioBankNodeLeftOffset, left_link);
    Store(node + audio::kWindowsAudioBankNodeParentOffset, parent_link);
    Store(node + audio::kWindowsAudioBankNodeRightOffset, right_link);
    Store(node + audio::kWindowsAudioBankNodeColorOffset,
          static_cast<std::uint32_t>(parent_link ? 0U : 1U));
    StoreCString(name_address, name);
}

void InitializeFake(FakeContext* state, bool no_sound = false) noexcept
{
    std::memset(g_image, 0, sizeof(g_image));
    std::memset(state, 0, sizeof(*state));
    state->revision = 41U;
    state->lock_available = true;

    Store(kEngineBase + audio::kWindowsAudioSingletonGlobalRva, kAudio);
    Store(kAudio,
          kEngineBase + (no_sound ?
              audio::kWindowsAudioNoSoundVtableRva :
              audio::kWindowsAudioFmodVtableRva));
    if (!no_sound) {
        Store(kAudio + audio::kWindowsAudioFmodBankManagerOffset,
              kBankManager);
        Store(kBankManager + audio::kWindowsAudioBankTreeRootOffset,
              kBankNode + audio::kWindowsAudioBankNodeLinkOffset);
        Store(kBankManager + audio::kWindowsAudioBankTreeCountOffset,
              static_cast<std::uint32_t>(1U));
        InitializeBankNode(kBankNode, "menu_main", kBankName,
                           0, 0, 0, 0xA1B2C3D4U, false);
    }

    Store(kEngineBase + audio::kWindowsGameGlobalRva, kGame);
    Store(kGame, kEngineBase + audio::kWindowsGameVtableRva);
    Store(kGame + audio::kWindowsGameAudioSettingsOffset, kSettings);
    Store(kEngineBase + audio::kWindowsAudioVolumeGrainRva,
          0x3F800000U);
    Store(kSettings + audio::kWindowsAudioSettingsFxVolumeOffset,
          0x3F400000U);
    Store(kSettings + audio::kWindowsAudioSettingsWaveVolumeOffset,
          0x3F000000U);
    Store(kSettings + audio::kWindowsAudioSettingsSpeechVolumeOffset,
          0x3E800000U);
    Store(kSettings + audio::kWindowsAudioSettingsMovieVolumeOffset,
          0x3E400000U);
    Store(kSettings +
              audio::kWindowsAudioSettingsMovieVolumeSourceOffset,
          0x3E000000U);
    const char* global_settings = "audio_global_settings.scr";
    const std::uint32_t global_settings_length =
        static_cast<std::uint32_t>(std::strlen(global_settings));
    const StringHeader global_settings_header = {
        kGlobalSettings, global_settings_length, global_settings_length};
    Store(kSettings + audio::kWindowsAudioSettingsGlobalSelectorOffset,
          global_settings_header);
    StoreCString(kGlobalSettings, global_settings);

    const char* script = "data/audio/audiobanks.scr";
    const std::uint32_t script_length =
        static_cast<std::uint32_t>(std::strlen(script));
    const StringHeader script_header = {
        kScriptPath, script_length, script_length};
    const VectorHeader script_vector = {kScriptHeaders, 1U, 1U};
    Store(kEngineBase + audio::kWindowsAudioScriptPathVectorRva,
          script_vector);
    Store(kScriptHeaders, script_header);
    StoreCString(kScriptPath, script);

    Store(kEngineBase + audio::kWindowsSpeechManagerGlobalRva,
          kSpeechManager);
    const VectorHeader speech_vector = {kSpeechPointers, 1U, 1U};
    Store(kSpeechManager + audio::kWindowsSpeechBankVectorOffset,
          speech_vector);
    Store(kSpeechManager + audio::kWindowsSpeechBankVectorTagOffset,
          audio::kWindowsSpeechBankVectorTag);
    Store(kSpeechPointers, kSpeechBank);
    Store(kSpeechBank + audio::kWindowsSpeechBankLocalisedOffset,
          static_cast<std::uint8_t>(1U));
    Store(kSpeechBank + audio::kWindowsSpeechBankHashOffset,
          0x10203040U);
    Store(kAlternateSpeechBank +
              audio::kWindowsSpeechBankLocalisedOffset,
          static_cast<std::uint8_t>(0U));
    Store(kAlternateSpeechBank + audio::kWindowsSpeechBankHashOffset,
          0x50607080U);
}

audio::AudioBaselineWindowsReadContext Context(
    FakeContext* state) noexcept
{
    audio::AudioBaselineWindowsReadContext context = {};
    context.engine_base = kEngineBase;
    context.read_memory = ReadFakeMemory;
    context.read_fence = ReadFakeFence;
    context.try_enter_audio_lock = TryFakeLock;
    context.leave_audio_lock = LeaveFakeLock;
    context.callback_user = state;
    context.exact_engine_image_validated = true;
    context.speech_registry_synchronization_proven = true;
    return context;
}

bool HasReason(std::uint64_t mask,
               audio::AudioBaselineWindowsSnapshotReason reason) noexcept
{
    return (mask & static_cast<std::uint64_t>(reason)) != 0;
}

bool TestCompleteFmodRawSnapshot() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    /* A retained native-file object is opaque identity only.  Snapshotting
     * must not dereference it to guess a provider. */
    state.unreadable_address = kNativeFile;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot first = {};
    audio::AudioBaselineWindowsSnapshot second = {};
    audio::AudioBaselineWindowsSnapshot relocated = {};
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &first));
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &second));
    CHECK(first.sound_lock_snapshot_complete);
    CHECK(first.raw_snapshot_complete);
    CHECK(!first.authoritative_b0_complete);
    CHECK(first.unresolved_fields != 0);
    CHECK(first.complete_fields == audio::kAudioWindowsRequiredRawFields);
    CHECK(first.backend_kind ==
          audio::AudioBaselineWindowsBackendKind::Fmod);
    CHECK(first.audio_singleton == kAudio);
    CHECK(first.bank_manager == kBankManager);
    CHECK(first.bank_count == 1U);
    CHECK(first.banks[0].record == kBankNode);
    CHECK(first.banks[0].native_file == kNativeFile);
    CHECK(first.banks[0].tree_key == BankNameHash("menu_main"));
    CHECK(first.banks[0].record_hash == 0xA1B2C3D4U);
    CHECK(first.banks[0].tree_color == 1U);
    CHECK(std::strcmp(first.banks[0].name, "menu_main") == 0);
    CHECK(first.script_count == 1U);
    CHECK(std::strcmp(first.scripts[0].logical_path,
                      "data/audio/audiobanks.scr") == 0);
    CHECK(std::strcmp(first.settings.global_settings,
                      "audio_global_settings.scr") == 0);
    CHECK(first.settings.global_settings_fingerprint != 0);
    CHECK(first.speech_manager == kSpeechManager);
    CHECK(first.speech_bank_count == 1U);
    CHECK(first.speech_banks[0].bank == kSpeechBank);
    CHECK(first.speech_banks[0].bank_hash == 0x10203040U);
    CHECK(first.speech_banks[0].descriptor_fingerprint != 0);
    CHECK(first.speech_banks[0].localised);
    CHECK(first.speech_vector_observation_stable);
    CHECK(first.speech_owner_synchronization_proven);
    CHECK((first.unresolved_fields &
           audio::AudioWindowsUnresolvedSpeechSynchronization) == 0);
    CHECK(!first.speech_baseline_complete);
    CHECK(first.raw_snapshot_fingerprint ==
          second.raw_snapshot_fingerprint);
    CHECK(first.raw_snapshot_fingerprint != 0);
    StoreCString(kAlternateGlobalSettings,
                 "audio_global_settings.scr");
    const StringHeader relocated_global = {
        kAlternateGlobalSettings,
        static_cast<std::uint32_t>(
            std::strlen("audio_global_settings.scr")),
        static_cast<std::uint32_t>(
            std::strlen("audio_global_settings.scr") + 16U)};
    Store(kSettings + audio::kWindowsAudioSettingsGlobalSelectorOffset,
          relocated_global);
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &relocated));
    CHECK(std::memcmp(first.settings.pod, relocated.settings.pod,
                      sizeof(first.settings.pod)) != 0);
    CHECK(first.settings.fingerprint == relocated.settings.fingerprint);
    CHECK(first.raw_snapshot_fingerprint ==
          relocated.raw_snapshot_fingerprint);
    CHECK(state.lock_attempts == 3U && state.lock_leaves == 3U);
    CHECK(state.fence_reads == 6U);
    return true;
}

bool TestUnprovenSpeechSynchronizationNeverReadsSpeech() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    auto context = Context(&state);
    context.speech_registry_synchronization_proven = false;
    /* If the gated path touches any speech address this unreadable global
     * makes the capture fail with a memory-read reason. */
    state.unreadable_address =
        kEngineBase + audio::kWindowsSpeechManagerGlobalRva;
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(result.sound_lock_snapshot_complete);
    CHECK(!result.raw_snapshot_complete);
    CHECK(!result.speech_vector_observation_stable);
    CHECK(!result.speech_owner_synchronization_proven);
    CHECK(!result.speech_baseline_complete);
    CHECK(result.speech_manager == 0);
    CHECK(result.speech_bank_count == 0U);
    CHECK(result.speech_bank_manifest_fingerprint == 0U);
    CHECK(state.speech_registry_reads == 0U);
    CHECK(!HasReason(result.reason_mask,
                     audio::AudioWindowsReasonMemoryUnreadable));
    CHECK((result.unresolved_fields &
           audio::AudioWindowsUnresolvedSpeechSynchronization) != 0);
    /* Only one raw pass is needed while the validated audio lock is held. */
    CHECK(state.settings_pod_reads == 1U);
    CHECK(state.lock_attempts == 1U && state.lock_leaves == 1U);
    CHECK(state.fence_reads == 2U);
    return true;
}

bool TestNoSoundBackendHasExactEmptyFmodInventory() noexcept
{
    FakeContext state;
    InitializeFake(&state, true);
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(result.backend_kind ==
          audio::AudioBaselineWindowsBackendKind::NoSound);
    CHECK(result.bank_manager == 0 && result.bank_tree_root == 0);
    CHECK(result.bank_count == 0);
    CHECK(result.bank_manifest_fingerprint != 0);
    return true;
}

bool TestEmptyGlobalSettingsSelectorIsNatural() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    const StringHeader empty = {};
    Store(kSettings + audio::kWindowsAudioSettingsGlobalSelectorOffset,
          empty);
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(result.settings.global_settings_length == 0U);
    CHECK(result.settings.global_settings[0] == '\0');
    CHECK(result.settings.global_settings_fingerprint != 0);
    return true;
}

bool TestUnsupportedImageStopsBeforeCallbacks() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    auto context = Context(&state);
    context.exact_engine_image_validated = false;
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonUnsupportedImage));
    CHECK(state.fence_reads == 0 && state.lock_attempts == 0 &&
          state.lock_leaves == 0);
    return true;
}

bool TestObserverBusyStopsBeforeLock() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.active_calls = 1U;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonObserverBusy));
    CHECK(state.lock_attempts == 0 && state.lock_leaves == 0);
    return true;
}

bool TestObservationLossStopsBeforeLock() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.observation_loss = true;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonObservationLoss));
    CHECK(state.lock_attempts == 0 && state.lock_leaves == 0);
    return true;
}

bool TestLockBusyIsNonblocking() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.lock_available = false;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonAudioLockBusy));
    CHECK(state.lock_attempts == 1U && state.lock_leaves == 0U);
    return true;
}

bool TestBackendIdentityMismatchReleasesLock() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kAudio, static_cast<std::uintptr_t>(0xBADU));
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBackendIdentityMismatch));
    CHECK(state.lock_leaves == 1U);
    return true;
}

bool TestBankHeaderMismatchFailsClosed() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kBankManager + audio::kWindowsAudioBankTreeCountOffset,
          static_cast<std::uint32_t>(0U));
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankHeaderInvalid));
    CHECK(state.lock_leaves == 1U);
    return true;
}

bool TestBankCycleFailsClosed() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    const std::uintptr_t link =
        kBankNode + audio::kWindowsAudioBankNodeLinkOffset;
    Store(kBankNode + audio::kWindowsAudioBankNodeLeftOffset, link);
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankTreeCycle));
    return true;
}

bool TestBankHashMismatchFailsClosed() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kBankNode + audio::kWindowsAudioBankNodeHashOffset,
          0xDEADBEEFU);
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankHashMismatch));
    return true;
}

bool TestBankTreeColorValidation() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kBankNode + audio::kWindowsAudioBankNodeColorOffset,
          static_cast<std::uint32_t>(0U));
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankNodeInvalid));

    InitializeFake(&state);
    Store(kBankNode + audio::kWindowsAudioBankNodeColorOffset,
          static_cast<std::uint32_t>(2U));
    context = Context(&state);
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankNodeInvalid));
    return true;
}

bool TestUnterminatedBankNameFailsClosed() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kBankName + std::strlen("menu_main"), 'x');
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonBankNameInvalid));
    return true;
}

bool TestScriptCountBound() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    VectorHeader header = {
        kScriptHeaders,
        static_cast<std::uint32_t>(audio::kWindowsAudioScriptCapacity + 1U),
        static_cast<std::uint32_t>(audio::kWindowsAudioScriptCapacity + 1U)};
    Store(kEngineBase + audio::kWindowsAudioScriptPathVectorRva, header);
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonScriptCountOutOfRange));
    return true;
}

bool TestSnapshotChangeIsRejected() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.change_settings_between_passes = true;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonSnapshotChanged));
    CHECK(state.lock_leaves == 1U);
    return true;
}

bool TestFenceRevisionChangeIsRejected() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.change_fence_revision = true;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonFenceChanged));
    CHECK(state.lock_leaves == 1U && state.fence_reads == 2U);
    return true;
}

bool TestUnreadableBankNodeReleasesLock() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.unreadable_address = kBankNode;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonMemoryUnreadable));
    CHECK(state.lock_leaves == 1U);
    return true;
}

bool TestSpeechPointerChangeWithinPassIsRejected() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    state.change_speech_pointer_within_pass = true;
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonSpeechVectorChanged));
    return true;
}

bool TestSpeechRecordValidation() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    Store(kSpeechBank + audio::kWindowsSpeechBankLocalisedOffset,
          static_cast<std::uint8_t>(2U));
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonSpeechRecordInvalid));
    return true;
}

bool TestTwoNodeTreeUsesDirectRootAndOrderedKeys() noexcept
{
    FakeContext state;
    InitializeFake(&state);
    const char* low_name = "ambient";
    const char* high_name = "menu_main";
    const std::uint32_t low_hash = BankNameHash(low_name);
    const std::uint32_t high_hash = BankNameHash(high_name);
    CHECK(low_hash < high_hash);
    const std::uintptr_t root_link =
        kBankNode + audio::kWindowsAudioBankNodeLinkOffset;
    const std::uintptr_t left_link =
        kBankNode2 + audio::kWindowsAudioBankNodeLinkOffset;
    InitializeBankNode(kBankNode, high_name, kBankName,
                       0, left_link, 0, 0x22U, false);
    InitializeBankNode(kBankNode2, low_name, kBankName2,
                       root_link, 0, 0, 0x11U, true);
    Store(kBankManager + audio::kWindowsAudioBankTreeCountOffset,
          static_cast<std::uint32_t>(2U));
    auto context = Context(&state);
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(audio::SnapshotAudioBaselineWindows(&context, &result));
    CHECK(result.bank_count == 2U);
    CHECK(result.banks[0].tree_key == low_hash);
    CHECK(result.banks[1].tree_key == high_hash);
    CHECK(result.banks[0].localised);
    return true;
}

bool TestInvalidContextIsRejected() noexcept
{
    audio::AudioBaselineWindowsSnapshot result = {};
    CHECK(!audio::SnapshotAudioBaselineWindows(nullptr, &result));
    CHECK(HasReason(result.reason_mask,
                    audio::AudioWindowsReasonInvalidArgument));
    return true;
}

using TestFn = bool (*)() noexcept;

struct TestCase {
    const char* name;
    TestFn function;
};

}  // namespace

int main()
{
    const TestCase tests[] = {
        {"complete_fmod_raw_snapshot", TestCompleteFmodRawSnapshot},
        {"unproven_speech_is_never_read",
         TestUnprovenSpeechSynchronizationNeverReadsSpeech},
        {"no_sound_empty_fmod_inventory",
         TestNoSoundBackendHasExactEmptyFmodInventory},
        {"empty_global_settings_selector",
         TestEmptyGlobalSettingsSelectorIsNatural},
        {"unsupported_image", TestUnsupportedImageStopsBeforeCallbacks},
        {"observer_busy", TestObserverBusyStopsBeforeLock},
        {"observation_loss", TestObservationLossStopsBeforeLock},
        {"nonblocking_lock", TestLockBusyIsNonblocking},
        {"backend_identity", TestBackendIdentityMismatchReleasesLock},
        {"bank_header", TestBankHeaderMismatchFailsClosed},
        {"bank_cycle", TestBankCycleFailsClosed},
        {"bank_hash", TestBankHashMismatchFailsClosed},
        {"bank_tree_color", TestBankTreeColorValidation},
        {"bank_name", TestUnterminatedBankNameFailsClosed},
        {"script_bound", TestScriptCountBound},
        {"double_pass", TestSnapshotChangeIsRejected},
        {"observer_fence", TestFenceRevisionChangeIsRejected},
        {"lock_release", TestUnreadableBankNodeReleasesLock},
        {"speech_pointer_stability",
         TestSpeechPointerChangeWithinPassIsRejected},
        {"speech_record", TestSpeechRecordValidation},
        {"direct_root_order", TestTwoNodeTreeUsesDirectRootAndOrderedKeys},
        {"invalid_context", TestInvalidContextIsRejected},
    };
    constexpr std::size_t count = sizeof(tests) / sizeof(tests[0]);
    for (std::size_t index = 0; index < count; ++index) {
        if (!tests[index].function()) {
            std::fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
        std::printf("[PASS] %s\n", tests[index].name);
    }
    std::printf("audio_baseline_windows_snapshot_fake_harness: %zu/%zu ok\n",
                count, count);
    return 0;
}
