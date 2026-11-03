# Editor appearance

Open the DLUMA page in Editor options and choose **System**, **Light**, or **Dark** under **Theme**. System follows the Windows app appearance. Changes apply while the Editor is open and also update the visual quest editor.

Windows High Contrast colors take precedence over the selected theme. On older Windows versions, the title bar may continue to use the Windows frame appearance.

Native adapters recognize the 64-bit Editor 6.0.0.1 build with SHA-256 `cacd9c96ba5d8c35a23170dc8be8029db9aa5e6223f5e6cd5de3e61d8e72da28` and its bundled QuestEditor, MEControl, Scintilla 3.0.4, and MFC 10 components. Replaced component binaries can retain their existing colors. Quest source keeps the Editor's Consolas 10 default and native font preferences.

When a custom startup image is included, it runs on its own UI thread. The lower-left corner shows the DLUMA version and commit, the Editor build, and startup progress. DLUMA hides the native splash when it loads; the original image can appear briefly before then. See [Custom startup image](../assets/editor-splash/README.md) for local build instructions.

The Source/Graph window integration is described in [Editor panels](EDITOR_PANELS.md).
