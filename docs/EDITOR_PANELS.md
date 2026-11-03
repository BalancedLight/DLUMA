# Editor panels

The native panel host provides tabs, layout, visibility, focus and child-process lifetime handling for Editor tools. The Quest editor uses it for its Source and Graph views. The C host lives in [dlw_editor_panel_host.h](../src/dlw_editor_panel_host.h); the WPF companion uses [EditorPanelHost](../workbench/Dluma.QuestGraph/Services/EditorPanelHost.cs).

These are repository integration components. The packaged Quest Graph connects through the authenticated Quest bridge. A tool's transport and document model supply its process authentication, revision checks, save and close decisions; the panel host handles its windows.

## Native host

Call the panel functions on the native owner's UI thread. Initialize the reference, then create a host around an existing source control:

```c
DlumaEditorPanelRef panel = {0};
panel.size = sizeof(panel);
panel.version = 1;

if (!dluma_editor_panel_host_create(owner, source, L"Source", L"Graph",
        on_panel_event, context, &panel))
    return 0;
```

Keep the reference for that host's lifetime. Call `dluma_editor_panel_host_layout` after native layout and `dluma_editor_panel_host_show` to switch views. The source retains its native parent and notifications.

Pass `content_window` and the host token to the authenticated companion. Its child window must already have that parent and the `WS_CHILD` style. Bind it with `dluma_editor_panel_host_bind`, the expected process ID and a live process handle. The host duplicates the handle and watches that process. Use `dluma_editor_panel_host_binding_generation` to identify the current binding.

Notifications borrow the panel reference for the callback and run on the owner's UI thread:

| Event | Meaning |
| --- | --- |
| `VISIBILITY` | Argument 1 selects the panel; 0 selects Source. |
| `RESIZED` | The native host has applied its current layout. |
| `FOCUS` | Focus was requested for the bound child. |
| `CHILD_EXITED` | The bound companion process exited. |
| `CLOSING` | Retire the associated tool session. |

Unbind a retiring child before binding its replacement. Close the host when its native owner closes. Drain `dluma_editor_panel_host_callbacks_drained` after all hosts close and before unloading the native module.

## Companion window

The WPF implementation matches the Editor parent's DPI awareness before creating WPF windows. After authenticating the Editor connection, it validates the parent process and creates an `HwndSource` with `ParentWindow = content_window` and the `WS_CHILD` style. It submits that HWND through the bridge's panel-ready handshake and retains the returned binding generation.

The registered focus message carries the panel token and binding generation. The companion checks both before focusing its content. A disconnected Editor session closes the embedded view and its companion process. Each document keeps its own authenticated identity and revision state while sharing the panel implementation.

Appearance is provided by the shared Editor theme resources; see [Editor appearance](EDITOR_APPEARANCE.md).
