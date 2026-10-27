#include "fixes/custom_map_asset_isolation/FilesystemSourceInventory.h"
#include "fixes/custom_map_asset_isolation/FilesystemSourceWindowsView.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace vfs =
    dluma::fixes::custom_map_asset_isolation::vfs;

static_assert(std::is_trivially_copyable<
                  vfs::FilesystemSourceInventory>::value,
              "inventory must remain bounded POD");
static_assert(std::is_standard_layout<
                  vfs::FilesystemSourceInventory>::value,
              "inventory must remain standard-layout POD");

namespace {

int g_passes;
int g_failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::printf("check failed: %s:%d: %s\n", __FILE__, __LINE__,     \
                        #expression);                                         \
            return false;                                                     \
        }                                                                     \
    } while (0)

struct FakeSource {
    const char* path;
    bool readable;
};

struct FakeNode {
    std::uintptr_t source;
    std::uintptr_t next;
    std::uintptr_t previous;
};

struct FakeRegistry {
    FakeNode nodes[64];
    FakeSource sources[64];
    char generated_paths[64][64];
    std::uintptr_t unreadable_address;
    std::uint32_t copy_calls;
};

std::uintptr_t Link(FakeRegistry* registry, std::size_t index) noexcept
{
    return reinterpret_cast<std::uintptr_t>(&registry->nodes[index].next);
}

void InitializeGoodRegistry(FakeRegistry* registry) noexcept
{
    std::memset(registry, 0, sizeof(*registry));
    registry->sources[0] = {"X:\\Game\\DW\\Data", true};
    registry->sources[1] = {"X:\\Game\\DW_DLC1\\DataDLC1_0.pak", true};
    registry->sources[2] = {nullptr, true};
    for (std::size_t index = 0; index < 3; ++index)
        registry->nodes[index].source =
            reinterpret_cast<std::uintptr_t>(&registry->sources[index]);
    registry->nodes[0].next = Link(registry, 1);
    registry->nodes[0].previous = 0;
    registry->nodes[1].next = Link(registry, 2);
    registry->nodes[1].previous = Link(registry, 0);
    registry->nodes[2].next = 0;
    registry->nodes[2].previous = Link(registry, 1);
}

bool ReadPointer(void* user, std::uintptr_t address,
                 std::uintptr_t* output) noexcept
{
    FakeRegistry* registry = static_cast<FakeRegistry*>(user);
    if (!registry || !output || address == registry->unreadable_address)
        return false;
    for (std::size_t index = 0; index < 64; ++index) {
        const std::uintptr_t node =
            reinterpret_cast<std::uintptr_t>(&registry->nodes[index]);
        if (address >= node &&
            address + sizeof(std::uintptr_t) <= node + sizeof(FakeNode)) {
            std::memcpy(output, reinterpret_cast<const void*>(address),
                        sizeof(*output));
            return true;
        }
    }
    return false;
}

bool CopyPath(void* user, std::uintptr_t source,
              char* output, std::size_t capacity) noexcept
{
    FakeRegistry* registry = static_cast<FakeRegistry*>(user);
    if (!registry || !output || !capacity) return false;
    ++registry->copy_calls;
    for (std::size_t index = 0; index < 64; ++index) {
        if (source != reinterpret_cast<std::uintptr_t>(
                          &registry->sources[index]))
            continue;
        const FakeSource& item = registry->sources[index];
        if (!item.readable || !item.path) return false;
        const std::size_t length = std::strlen(item.path);
        if (length + 1U > capacity) return false;
        std::memcpy(output, item.path, length + 1U);
        return true;
    }
    return false;
}

vfs::FilesystemSourceRegistryView GoodView(
    FakeRegistry* registry) noexcept
{
    vfs::FilesystemSourceRegistryView view = {};
    view.head_link = Link(registry, 0);
    view.tail_link = Link(registry, 2);
    view.fallback_link = Link(registry, 2);
    view.registry_generation = 41U;
    view.reported_count = 3;
    view.synchronization_held = true;
    view.layout_validated = true;
    view.registry_generation_observed = true;
    view.fallback_state_known = true;
    view.fallback_installed = true;
    return view;
}

vfs::FilesystemSourceInventoryReader Reader(
    FakeRegistry* registry) noexcept
{
    const vfs::FilesystemSourceInventoryReader reader = {
        &ReadPointer, &CopyPath, registry};
    return reader;
}

bool TestCompleteInventory()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    const auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.complete);
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::None);
    CHECK(inventory.source_count == 3);
    CHECK(inventory.reported_count == 3);
    CHECK(inventory.sources[0].list_ordinal == 1);
    CHECK(inventory.registry_generation_observed);
    CHECK(inventory.registry_generation == 41U);
    CHECK(inventory.sources[0].source == registry.nodes[0].source);
    CHECK(std::strcmp(inventory.sources[0].path,
                      "X:\\Game\\DW\\Data") == 0);
    CHECK(std::strcmp(inventory.sources[1].path,
                      "X:\\Game\\DW_DLC1\\DataDLC1_0.pak") == 0);
    CHECK(inventory.sources[2].native_fallback);
    CHECK(std::strcmp(inventory.sources[2].path,
                      "filesystem:native-fallback") == 0);
    CHECK(registry.copy_calls == 2);
    return true;
}

bool TestSynchronizationAndLayoutAreRequired()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    view.synchronization_held = false;
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::SynchronizationNotHeld);
    view.synchronization_held = true;
    view.layout_validated = false;
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::LayoutNotValidated);
    return true;
}

bool TestInvalidAndUnavailableRegistry()
{
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(nullptr, nullptr,
                                                  &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::InvalidArguments);
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    view.head_link = 0;
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::RegistryUnavailable);
    return true;
}

bool TestCapacityFailsBeforeWalking()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    view.reported_count = static_cast<std::uint32_t>(
        vfs::kFilesystemSourceInventoryCapacity + 1U);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::CapacityExceeded);
    CHECK(registry.copy_calls == 0);
    return true;
}

bool TestFallbackMustBeTail()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    view.fallback_link = Link(&registry, 1);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::FallbackMismatch);
    return true;
}

bool TestUnreadableLinkAndSourceFailClosed()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    const auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    registry.unreadable_address = Link(&registry, 1);
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::LinkUnreadable);
    registry.unreadable_address =
        reinterpret_cast<std::uintptr_t>(&registry.nodes[1].source);
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::SourceUnreadable);
    return true;
}

bool TestPreviousLinkMismatch()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    registry.nodes[1].previous = 0;
    const auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::PreviousLinkMismatch);
    return true;
}

bool TestCycleIsBounded()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    registry.nodes[1].next = Link(&registry, 0);
    registry.nodes[0].previous = Link(&registry, 1);
    auto view = GoodView(&registry);
    view.reported_count = 3;
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::DuplicateOrCyclicLink ||
          inventory.failure ==
              vfs::FilesystemSourceInventoryFailure::PreviousLinkMismatch);
    return true;
}

bool TestCountMismatchBothDirections()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    view.reported_count = 2;
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::CountMismatch);
    view.reported_count = 4;
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::CountMismatch);
    return true;
}

bool TestTailAndFallbackLinksAreChecked()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    auto view = GoodView(&registry);
    registry.nodes[2].next = Link(&registry, 3);
    registry.nodes[3].source =
        reinterpret_cast<std::uintptr_t>(&registry.sources[3]);
    registry.sources[3] = {"X:\\unexpected", true};
    registry.nodes[3].previous = Link(&registry, 2);
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::FallbackMismatch);
    InitializeGoodRegistry(&registry);
    view = GoodView(&registry);
    view.tail_link = Link(&registry, 1);
    view.fallback_link = Link(&registry, 1);
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::FallbackMismatch);
    return true;
}

bool TestPathFailureDoesNotBecomeAnIdentity()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    registry.sources[1].readable = false;
    const auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::PathUnavailable);
    CHECK(!inventory.complete);
    return true;
}

bool TestKnownAbsentFallbackIsValid()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    registry.nodes[1].next = 0;
    auto view = GoodView(&registry);
    view.tail_link = Link(&registry, 1);
    view.fallback_link = 0;
    view.reported_count = 2;
    view.fallback_installed = false;
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.complete);
    CHECK(inventory.source_count == 2);
    CHECK(inventory.fallback_state_known);
    CHECK(!inventory.fallback_installed);
    CHECK(!inventory.sources[1].native_fallback);
    CHECK(std::strcmp(inventory.sources[1].path,
                      "X:\\Game\\DW_DLC1\\DataDLC1_0.pak") == 0);
    CHECK(registry.copy_calls == 2);
    return true;
}

bool TestUnknownFallbackStateIsRejected()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    auto view = GoodView(&registry);
    view.fallback_state_known = false;
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(!vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::FallbackStateUnknown);
    return true;
}

bool TestFiftyFourSourceBaseline()
{
    FakeRegistry registry = {};
    std::memset(&registry, 0, sizeof(registry));
    constexpr std::size_t source_count = 54;
    for (std::size_t index = 0; index < source_count; ++index) {
        if (index + 1U == source_count) {
            registry.sources[index] = {nullptr, true};
        } else {
            std::snprintf(registry.generated_paths[index],
                          sizeof(registry.generated_paths[index]),
                          "X:\\Game\\DW_DLC%02zu\\Data%02zu.pak",
                          index, index);
            registry.sources[index] = {
                registry.generated_paths[index], true};
        }
        registry.nodes[index].source =
            reinterpret_cast<std::uintptr_t>(&registry.sources[index]);
        registry.nodes[index].previous = index
            ? Link(&registry, index - 1U) : 0;
        registry.nodes[index].next = index + 1U < source_count
            ? Link(&registry, index + 1U) : 0;
    }
    vfs::FilesystemSourceRegistryView view = {};
    view.head_link = Link(&registry, 0);
    view.tail_link = Link(&registry, source_count - 1U);
    view.fallback_link = view.tail_link;
    view.registry_generation = 54;
    view.reported_count = static_cast<std::uint32_t>(source_count);
    view.synchronization_held = true;
    view.layout_validated = true;
    view.registry_generation_observed = true;
    view.fallback_state_known = true;
    view.fallback_installed = true;
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory inventory = {};
    CHECK(vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &inventory));
    CHECK(inventory.complete);
    CHECK(inventory.reported_count == source_count);
    CHECK(inventory.source_count == source_count);
    CHECK(inventory.sources[53].native_fallback);
    CHECK(registry.copy_calls == 53);
    return true;
}

struct FakeWindowsView {
    alignas(16) unsigned char module[
        vfs::kFilesystemSourcesSrwLockRva + 32U];
    FakeRegistry registry;
    std::uint64_t generation;
    std::uint32_t generation_reads;
    bool change_generation;
    bool observation_idle;
};

bool ReadWindowsPointer(void* user, std::uintptr_t address,
                        std::uintptr_t* output) noexcept
{
    FakeWindowsView* windows = static_cast<FakeWindowsView*>(user);
    if (!windows || !output) return false;
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(windows->module);
    if (address == base + vfs::kFilesystemSourceHeadRva ||
        address == base + vfs::kFilesystemSourceTailRva ||
        address == base + vfs::kFilesystemFallbackLinkRva) {
        std::memcpy(output, reinterpret_cast<const void*>(address),
                    sizeof(*output));
        return true;
    }
    return ReadPointer(&windows->registry, address, output);
}

bool ReadWindowsCount(void* user, std::uintptr_t address,
                      std::uint32_t* output) noexcept
{
    FakeWindowsView* windows = static_cast<FakeWindowsView*>(user);
    if (!windows || !output) return false;
    const std::uintptr_t expected =
        reinterpret_cast<std::uintptr_t>(windows->module) +
        vfs::kFilesystemSourceCountRva;
    if (address != expected) return false;
    std::memcpy(output, reinterpret_cast<const void*>(address),
                sizeof(*output));
    return true;
}

bool CopyWindowsPath(void* user, std::uintptr_t source,
                     char* output, std::size_t capacity) noexcept
{
    FakeWindowsView* windows = static_cast<FakeWindowsView*>(user);
    return windows && CopyPath(
        &windows->registry, source, output, capacity);
}

std::uint64_t ReadWindowsGeneration(void* user) noexcept
{
    FakeWindowsView* windows = static_cast<FakeWindowsView*>(user);
    if (!windows) return 0;
    if (windows->change_generation && windows->generation_reads != 0)
        ++windows->generation;
    ++windows->generation_reads;
    return windows->generation;
}

bool WindowsObservationIdle(void* user) noexcept
{
    const FakeWindowsView* windows =
        static_cast<const FakeWindowsView*>(user);
    return windows && windows->observation_idle;
}

void InitializeWindowsView(FakeWindowsView* windows) noexcept
{
    std::memset(windows, 0, sizeof(*windows));
    InitializeGoodRegistry(&windows->registry);
    windows->generation = 90;
    windows->observation_idle = true;
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(windows->module);
    const std::uintptr_t head = Link(&windows->registry, 0);
    const std::uintptr_t tail = Link(&windows->registry, 2);
    const std::uint32_t count = 3;
    std::memcpy(reinterpret_cast<void*>(
                    base + vfs::kFilesystemSourceHeadRva),
                &head, sizeof(head));
    std::memcpy(reinterpret_cast<void*>(
                    base + vfs::kFilesystemSourceTailRva),
                &tail, sizeof(tail));
    std::memcpy(reinterpret_cast<void*>(
                    base + vfs::kFilesystemSourceCountRva),
                &count, sizeof(count));
    std::memcpy(reinterpret_cast<void*>(
                    base + vfs::kFilesystemFallbackLinkRva),
                &tail, sizeof(tail));
}

vfs::FilesystemSourceWindowsBinding WindowsBinding(
    FakeWindowsView* windows) noexcept
{
    const vfs::FilesystemSourceWindowsBinding binding = {
        reinterpret_cast<std::uintptr_t>(windows->module),
        &ReadWindowsPointer,
        &ReadWindowsCount,
        &CopyWindowsPath,
        &ReadWindowsGeneration,
        &WindowsObservationIdle,
        windows,
        true,
        true,
        true};
    return binding;
}

bool TestWindowsViewGenerationFence()
{
    FakeWindowsView windows = {};
    InitializeWindowsView(&windows);
    const auto binding = WindowsBinding(&windows);
    vfs::FilesystemSourceInventory inventory = {};
    vfs::FilesystemSourceWindowsViewDiagnostics diagnostics = {};
    CHECK(vfs::CaptureLiveFilesystemSourceRegistryView(
        &binding, &inventory, &diagnostics));
    CHECK(diagnostics.filesystem_registry_view_available);
    CHECK(diagnostics.filesystem_registry_lock_acquired);
    CHECK(diagnostics.filesystem_registry_fallback_known);
    CHECK(diagnostics.filesystem_registry_fallback_installed);
    CHECK(diagnostics.filesystem_registry_count == 3);
    CHECK(diagnostics.filesystem_registry_generation_before == 90);
    CHECK(diagnostics.filesystem_registry_generation_after == 90);

    InitializeWindowsView(&windows);
    windows.change_generation = true;
    CHECK(!vfs::CaptureLiveFilesystemSourceRegistryView(
        &binding, &inventory, &diagnostics));
    CHECK(inventory.failure ==
          vfs::FilesystemSourceInventoryFailure::
              RegistryChangedDuringCapture);
    CHECK(diagnostics.filesystem_registry_generation_before !=
          diagnostics.filesystem_registry_generation_after);
    return true;
}
bool TestSamePathDifferentExactObjectIsNotBaselineOwned()
{
    FakeRegistry registry = {};
    InitializeGoodRegistry(&registry);
    const auto view = GoodView(&registry);
    const auto reader = Reader(&registry);
    vfs::FilesystemSourceInventory baseline = {};
    CHECK(vfs::CaptureFilesystemSourceInventory(
        &view, &reader, &baseline));
    vfs::FilesystemSourceInventory current = baseline;
    CHECK(vfs::ExactFilesystemSourceInventoryIdentityMatches(
        &baseline, &current));
    FakeSource replacement = {"X:\\Game\\DW\\Data", true};
    current.sources[0].source =
        reinterpret_cast<std::uintptr_t>(&replacement);
    CHECK(std::strcmp(current.sources[0].path,
                      baseline.sources[0].path) == 0);
    CHECK(!vfs::ExactFilesystemSourceInventoryIdentityMatches(
        &baseline, &current));
    return true;
}
bool TestFailureNamesAreDeterministic()
{
    CHECK(std::strcmp(vfs::FilesystemSourceInventoryFailureName(
                          vfs::FilesystemSourceInventoryFailure::None),
                      "none") == 0);
    CHECK(std::strcmp(vfs::FilesystemSourceInventoryFailureName(
                          vfs::FilesystemSourceInventoryFailure::
                              DuplicateOrCyclicLink),
                      "duplicate_or_cyclic_link") == 0);
    CHECK(std::strcmp(vfs::FilesystemSourceInventoryFailureName(
                          static_cast<
                              vfs::FilesystemSourceInventoryFailure>(255)),
                      "unknown") == 0);
    return true;
}

void Run(const char* name, bool (*test)())
{
    if (test()) {
        ++g_passes;
        std::printf("PASS %s\n", name);
    } else {
        ++g_failures;
        std::printf("FAIL %s\n", name);
    }
}

}  // namespace

int main()
{
    Run("complete_inventory", &TestCompleteInventory);
    Run("synchronization_and_layout", &TestSynchronizationAndLayoutAreRequired);
    Run("invalid_and_unavailable", &TestInvalidAndUnavailableRegistry);
    Run("capacity", &TestCapacityFailsBeforeWalking);
    Run("fallback_tail", &TestFallbackMustBeTail);
    Run("unreadable", &TestUnreadableLinkAndSourceFailClosed);
    Run("previous_link", &TestPreviousLinkMismatch);
    Run("cycle", &TestCycleIsBounded);
    Run("count", &TestCountMismatchBothDirections);
    Run("tail_and_fallback", &TestTailAndFallbackLinksAreChecked);
    Run("known_absent_fallback", &TestKnownAbsentFallbackIsValid);
    Run("unknown_fallback", &TestUnknownFallbackStateIsRejected);
    Run("fifty_four_sources", &TestFiftyFourSourceBaseline);
    Run("windows_generation_fence", &TestWindowsViewGenerationFence);
    Run("same_path_different_object",
        &TestSamePathDifferentExactObjectIsNotBaselineOwned);
    Run("path_failure", &TestPathFailureDoesNotBecomeAnIdentity);
    Run("failure_names", &TestFailureNamesAreDeterministic);
    std::printf("%d passed, %d failed\n", g_passes, g_failures);
    return g_failures ? 1 : 0;
}
