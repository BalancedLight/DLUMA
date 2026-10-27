#ifndef DLUMA_FIXES_CANCELLED_PRESENTATION_CLEANUP_VERIFIER_H
#define DLUMA_FIXES_CANCELLED_PRESENTATION_CLEANUP_VERIFIER_H

#include "CustomContentLedger.h"
#include "FilesystemSourceInventory.h"

#include <cstddef>

namespace dluma::fixes::custom_map_asset_isolation::cancelled_cleanup {

/*
 * Proves that the second live filesystem inventory is exactly the first
 * inventory minus the source objects selected from the immutable ledger
 * snapshot. Every retained record must preserve object, link, fallback role,
 * and canonical path identity; no replacement record is accepted.
 */
bool VerifyFilesystemInventoryExactMinusSources(
    const vfs::FilesystemSourceInventory* before,
    const vfs::FilesystemSourceInventory* after,
    const content::SourceResetSnapshot* removed,
    std::size_t removed_count) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cancelled_cleanup

#endif
