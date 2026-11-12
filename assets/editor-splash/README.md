# Custom startup image

Place a GIF named `startup.gif` in this directory to include a custom animated startup image in a local build. The image is copied into `dluma_runtime/editor/` and recorded in the runtime manifest. Startup progress and version text are drawn over the image.

The GIF is optional and excluded from source control. Builds without it use the Editor's normal startup window.

For a source export intended to include the artwork, supply the exact local `startup.gif` in this directory before building. After the build, run `python tools/verify_package_manifest.py --package-dir <package-dir> --mod-name <loader-mod-name> --require-editor-splash` to require the image and verify its manifest-bound contents. Clean-source builds can omit the image and this option.
