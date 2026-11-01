# Menu animations

`dluma_thriller_ee.rpack` is built from `thriller.anm2` and
`menu_animations.json` using Dying Light ReAnimated. To rebuild it, run:

```text
DLReAnimated build-animation-rpack PakAssets/Animations/menu_animations.json PakAssets/Animations/dluma_thriller_ee.rpack
```

The animation and script resources use DLUMA-specific names to keep them
separate from other installed packs.

The loader PAK contains it at `Anims/dluma_thriller_ee.rpack`.
Player streams RPack files from disk, so the loader materializes a private
runtime cache copy before registering the embedded pack.
