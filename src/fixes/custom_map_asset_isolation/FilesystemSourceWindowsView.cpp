#define _HAS_EXCEPTIONS 0

#include "FilesystemSourceWindowsView.h"

#include "../../dlw_win32.h"

#include <cstring>

extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockShared(PVOID lock);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockShared(PVOID lock);
}

namespace dluma::fixes::custom_map_asset_isolation::vfs {
namespace {

bool AddRva(std::uintptr_t base, std::uintptr_t rva,
            std::uintptr_t* output) noexcept
{
    if (!output || !base || base > UINTPTR_MAX - rva) return false;
    *output = base + rva;
    return true;
}

void Fail(FilesystemSourceInventory* output,
          FilesystemSourceWindowsViewDiagnostics* diagnostics,
          FilesystemSourceInventoryFailure failure) noexcept
{
    if (output) {
        std::memset(output, 0, sizeof(*output));
        output->failure = failure;
    }
    if (diagnostics)
        diagnostics->filesystem_registry_inventory_result = failure;
}

}  // namespace

bool CaptureLiveFilesystemSourceRegistryView(
    const FilesystemSourceWindowsBinding* binding,
    FilesystemSourceInventory* output,
    FilesystemSourceWindowsViewDiagnostics* diagnostics) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (diagnostics)
        std::memset(diagnostics, 0, sizeof(*diagnostics));
    if (!binding || !output || !diagnostics ||
        !binding->read_pointer || !binding->read_count ||
        !binding->copy_path || !binding->read_generation ||
        !binding->observation_idle) {
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::InvalidArguments);
        return false;
    }
    if (!binding->supported_pe_identity_validated ||
        !binding->registry_layout_validated ||
        !binding->source_path_reader_validated) {
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::LayoutNotValidated);
        return false;
    }

    std::uintptr_t head_address = 0;
    std::uintptr_t tail_address = 0;
    std::uintptr_t count_address = 0;
    std::uintptr_t fallback_address = 0;
    std::uintptr_t lock_address = 0;
    if (!AddRva(binding->filesystem_base, kFilesystemSourceHeadRva,
                &head_address) ||
        !AddRva(binding->filesystem_base, kFilesystemSourceTailRva,
                &tail_address) ||
        !AddRva(binding->filesystem_base, kFilesystemSourceCountRva,
                &count_address) ||
        !AddRva(binding->filesystem_base, kFilesystemFallbackLinkRva,
                &fallback_address) ||
        !AddRva(binding->filesystem_base,
                kFilesystemSourcesSrwLockRva, &lock_address)) {
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::RegistryUnavailable);
        return false;
    }

    diagnostics->filesystem_registry_view_available = true;
    diagnostics->filesystem_registry_generation_before =
        binding->read_generation(binding->user);
    if (!binding->observation_idle(binding->user)) {
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::
                 RegistryChangedDuringCapture);
        return false;
    }

    PVOID lock = reinterpret_cast<PVOID>(lock_address);
    AcquireSRWLockShared(lock);
    diagnostics->filesystem_registry_lock_acquired = true;

    const bool globals_read =
        binding->read_pointer(
            binding->user, head_address,
            &diagnostics->filesystem_registry_head) &&
        binding->read_pointer(
            binding->user, tail_address,
            &diagnostics->filesystem_registry_tail) &&
        binding->read_count(
            binding->user, count_address,
            &diagnostics->filesystem_registry_count) &&
        binding->read_pointer(
            binding->user, fallback_address,
            &diagnostics->filesystem_registry_fallback_link);
    diagnostics->filesystem_registry_fallback_known = globals_read;
    diagnostics->filesystem_registry_fallback_installed =
        globals_read &&
        diagnostics->filesystem_registry_fallback_link != 0;

    bool captured = false;
    if (globals_read) {
        const FilesystemSourceRegistryView view = {
            diagnostics->filesystem_registry_head,
            diagnostics->filesystem_registry_tail,
            diagnostics->filesystem_registry_fallback_link,
            diagnostics->filesystem_registry_generation_before,
            diagnostics->filesystem_registry_count,
            true,
            true,
            true,
            true,
            diagnostics->filesystem_registry_fallback_installed};
        const FilesystemSourceInventoryReader reader = {
            binding->read_pointer, binding->copy_path, binding->user};
        captured = CaptureFilesystemSourceInventory(
            &view, &reader, output);
    } else {
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::RegistryUnavailable);
    }

    diagnostics->filesystem_registry_generation_after =
        binding->read_generation(binding->user);
    const bool generation_stable =
        binding->observation_idle(binding->user) &&
        diagnostics->filesystem_registry_generation_before ==
            diagnostics->filesystem_registry_generation_after;
    if (!generation_stable) {
        captured = false;
        Fail(output, diagnostics,
             FilesystemSourceInventoryFailure::
                 RegistryChangedDuringCapture);
    } else {
        diagnostics->filesystem_registry_inventory_result =
            output->failure;
    }
    ReleaseSRWLockShared(lock);
    return captured;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::vfs
