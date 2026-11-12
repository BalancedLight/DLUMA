#include "core/dlw_runtime_tree.h"
#include "dlw_editor_companion_runtime.h"
#include "generated/dluma_api_catalog.generated.h"
#include <string.h>

void *dluma_editor_companion_runtime_acquire(
    const WCHAR *runtime_root, const char *runtime_version)
{
    DlumaRuntimeTreeLease *lease;
    if (!runtime_root || !runtime_version) return 0;
    lease = (DlumaRuntimeTreeLease *)dluma_alloc(sizeof(*lease));
    if (!lease) return 0;
    memset(lease, 0, sizeof(*lease));
    lease->struct_size = (unsigned int)sizeof(*lease);
    if (!dluma_runtime_tree_acquire(runtime_root, runtime_version,
            DLUMA_API_CATALOG_SHA256, lease)) {
        dluma_log_pair("WARN", "editor_companion_private_runtime", lease->diagnostic);
        dluma_runtime_tree_release(lease);
        dluma_free(lease);
        return 0;
    }
    return lease;
}

void dluma_editor_companion_runtime_release(void *opaque)
{
    DlumaRuntimeTreeLease *lease = (DlumaRuntimeTreeLease *)opaque;
    if (!lease) return;
    dluma_runtime_tree_release(lease);
    dluma_free(lease);
}
