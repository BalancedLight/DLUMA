# HUD surfaces

The `dluma.ui.surface.v1` service provides up to four owner-bound RGBA8
surfaces, with one surface per mod. Query the `ui.surface` capability before
creating one. Calls run on the game thread and require the `ui.overlay` access
permission. Surfaces belong to the current map generation and are released
when their owner or map is retired.

Create accepts dimensions from 1 to 2048 pixels per side and returns an opaque
token. Update copies one complete frame during the call: pixels are RGBA8,
stride is in bytes, and the byte-view length must equal stride times height.
Stride must be at least width times four and a frame is limited to 16 MiB.
The buffer can contain zero bytes and need only remain valid for the call.

Layout uses a 1280 by 720 coordinate space. To present a 320 by 200 classic
pixel-art image with a 6:5 vertical pixel aspect, set its layout to 320 by 240
or a proportional multiple. Visibility is controlled separately. Releasing a
surface restores the controls suppressed by that owner.

The suppression mask uses these bits: health `1`, stamina `2`, radar `4`,
weapons `8`, crosshair `16`, and reload `32`. Combine them with bitwise OR.
Objectives, subtitles and interaction prompts are not included. Suppression
from multiple owners is combined while their surfaces remain visible.

The service is currently gated while its native Player backend is validated.
Clients must handle an unavailable capability and must not retain a token
across a map transition.
