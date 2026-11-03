# Custom startup image

Place a GIF named `startup.gif` in this directory to include a custom animated startup image in a local build. The image is copied into `dluma_runtime/editor/` and recorded in the runtime manifest. Startup progress and version text are drawn over the image.

The GIF is optional and excluded from source control. Builds without it use the Editor's normal startup window.
