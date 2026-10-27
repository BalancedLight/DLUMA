# Mod access and limits

Native DLL and C# mods run with the same Windows access as Dying Light. They can read or write files, use the network, and call native APIs. Approving a mod expresses trust in that package; it does not restrict native or managed code.

Lua mods run in the loader's sandbox. Their manifests declare required capabilities, and the loader applies configured memory, instruction, callback, timer, and logging limits. A capability may also be unavailable on a particular game build.

Package locks record exact files, sizes, and hashes. Changes to executable content, manifests, configuration schemas, or companion content can require renewed approval. Keep the package together and rebuild its lock through the package tools when authoring an update.

Mods receive lifecycle and API services through their assigned owner. A mod should release its resources during shutdown, handle unavailable capabilities, and avoid retaining game handles beyond their documented lifetime.

To report a security issue, follow [SECURITY.md](../SECURITY.md).
