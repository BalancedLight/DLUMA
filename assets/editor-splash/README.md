# Editor startup animation

`startup.gif` is a required, tracked Editor asset. Release, Debug, and nightly builds copy it into `dluma_runtime/editor/` and record its hash in the runtime manifest. Startup progress and version text are drawn over the animation.

Package and release validation require the animation, so a build missing it cannot become an auto-update package. Git source exports include the same artwork as a checkout.

To verify a package directly, run `python tools/verify_package_manifest.py --package-dir <package-dir> --mod-name <loader-mod-name> --require-editor-splash`.

The illustration contains Dying Light game artwork, characters, and Techland branding. These remain subject to their original rights and are excluded from DLUMA's MIT source-code license; see [third-party notices](../../THIRD_PARTY_NOTICES.md).
