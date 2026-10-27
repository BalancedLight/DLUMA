#define _HAS_EXCEPTIONS 0

#include "FixRegistry.h"
#include "custom_map_asset_isolation/CustomMapAssetIsolationFix.h"
#include "custom_map_campaign_credits_exit/CustomMapCampaignCreditsExitFix.h"
#include "custom_map_quest_level_name/CustomMapQuestLevelNameFix.h"
#include "main_menu_credits_music/MainMenuCreditsMusicFix.h"

extern "C" {
#include "../dlw_common.h"
}

#include <new>

namespace dluma::fixes {
namespace {

volatile LONG g_registry_lock;
bool g_initialized;
alignas(std::vector<FixStatus>) unsigned char g_status_storage[
    sizeof(std::vector<FixStatus>)];
std::vector<FixStatus>* g_statuses;

class RegistryLock {
public:
    RegistryLock()
    {
        while (dluma_atomic_compare_exchange(&g_registry_lock, 1, 0) != 0)
            Sleep(1);
    }

    ~RegistryLock()
    {
        dluma_atomic_exchange(&g_registry_lock, 0);
    }

    RegistryLock(const RegistryLock&) = delete;
    RegistryLock& operator=(const RegistryLock&) = delete;
};

std::vector<FixStatus>& Statuses()
{
    if (!g_statuses) {
        g_statuses = new (g_status_storage) std::vector<FixStatus>();
    }
    return *g_statuses;
}

bool ReadU16(uintptr_t address, uint16_t& value)
{
    if (!dluma_is_readable(reinterpret_cast<const void*>(address), sizeof(value)))
        return false;
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return true;
}

bool ReadU32(uintptr_t address, uint32_t& value)
{
    if (!dluma_is_readable(reinterpret_cast<const void*>(address), sizeof(value)))
        return false;
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return true;
}

bool ReadLoadedPeIdentity(HMODULE module, uintptr_t& base,
                          uint32_t& timestamp, uint32_t& image_size)
{
    uint16_t dos_signature = 0;
    uint16_t optional_magic = 0;
    uint32_t nt_offset = 0;
    uint32_t nt_signature = 0;

    base = reinterpret_cast<uintptr_t>(module);
    timestamp = 0;
    image_size = 0;
    if (!base || !ReadU16(base, dos_signature) || dos_signature != 0x5A4D ||
        !ReadU32(base + 0x3C, nt_offset) || nt_offset < 0x40 ||
        nt_offset > 0x00100000 ||
        !ReadU32(base + nt_offset, nt_signature) ||
        nt_signature != 0x00004550 ||
        !ReadU16(base + nt_offset + 24, optional_magic) ||
        optional_magic != 0x020B ||
        !ReadU32(base + nt_offset + 8, timestamp) ||
        !ReadU32(base + nt_offset + 24 + 56, image_size) ||
        image_size < 0x1000) {
        timestamp = 0;
        image_size = 0;
        return false;
    }
    return true;
}

bool FixIdEquals(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

void UpdateStatus(std::vector<FixStatus>& statuses,
                  const FixStatus& updated)
{
    for (FixStatus& current : statuses) {
        if (FixIdEquals(current.id, updated.id)) {
            current = updated;
            return;
        }
    }
    statuses.push_back(updated);
}

}  // namespace

RuntimeContext::RuntimeContext()
    : gamedll_module(nullptr), gamedll_base(0), gamedll_timestamp(0),
      gamedll_size_of_image(0), engine_module(nullptr), engine_base(0),
      engine_timestamp(0), engine_size_of_image(0),
      filesystem_module(nullptr), filesystem_base(0),
      filesystem_timestamp(0), filesystem_size_of_image(0)
{
}

RuntimeContext::RuntimeContext(HMODULE module)
    : gamedll_module(module), gamedll_base(0), gamedll_timestamp(0),
      gamedll_size_of_image(0),
      engine_module(GetModuleHandleW(L"engine_x64_rwdi.dll")),
      engine_base(0), engine_timestamp(0), engine_size_of_image(0),
      filesystem_module(GetModuleHandleW(L"filesystem_x64_rwdi.dll")),
      filesystem_base(0), filesystem_timestamp(0),
      filesystem_size_of_image(0)
{
    ReadLoadedPeIdentity(module, gamedll_base, gamedll_timestamp,
                         gamedll_size_of_image);
    ReadLoadedPeIdentity(engine_module, engine_base, engine_timestamp,
                         engine_size_of_image);
    ReadLoadedPeIdentity(filesystem_module, filesystem_base,
                         filesystem_timestamp, filesystem_size_of_image);
}

bool InitializeAllFixes(const RuntimeContext& context)
{
    RegistryLock lock;
    std::vector<FixStatus>& statuses = Statuses();

    if (g_initialized) return true;

    statuses.clear();
    /* Each fix is fail-open: its status is recorded, but its failure never
     * aborts forwarding, loader startup, Lua, lifecycle, or menu systems. */
    statuses.push_back(custom_map_quest_level_name::Initialize(
        context,
        g_dluma.config.custom_map_quest_level_name_crash_enabled != 0));
    statuses.push_back(main_menu_credits_music::Initialize(
        context, g_dluma.config.main_menu_credits_music_enabled != 0));
    /* The tail guard must be installed before the ownership observer can arm
     * it, while shutdown reverses that order so no observer can call an
     * unloaded guard. */
    statuses.push_back(custom_map_campaign_credits_exit::Initialize(
        context,
        g_dluma.config.custom_map_campaign_credits_exit_crash_enabled != 0,
        g_dluma.config.custom_map_asset_isolation_enabled != 0));
    statuses.push_back(custom_map_asset_isolation::Initialize(
        context,
        g_dluma.config.custom_map_asset_isolation_enabled != 0));
    g_initialized = true;
    return true;
}

void ShutdownAllFixes()
{
    RegistryLock lock;
    std::vector<FixStatus>& statuses = Statuses();

    if (!g_initialized) return;
    UpdateStatus(statuses, custom_map_asset_isolation::Shutdown());
    UpdateStatus(statuses, custom_map_campaign_credits_exit::Shutdown());
    UpdateStatus(statuses, main_menu_credits_music::Shutdown());
    UpdateStatus(statuses, custom_map_quest_level_name::Shutdown());
    g_initialized = false;
}

const std::vector<FixStatus>& GetFixStatuses()
{
    RegistryLock lock;
    std::vector<FixStatus>& statuses = Statuses();
    if (g_initialized)
        {
            UpdateStatus(statuses,
                         custom_map_campaign_credits_exit::CurrentStatus());
            UpdateStatus(statuses,
                         main_menu_credits_music::CurrentStatus());
            UpdateStatus(statuses,
                         custom_map_asset_isolation::CurrentStatus());
        }
    return statuses;
}

}  // namespace dluma::fixes
