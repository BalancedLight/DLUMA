# Editor appearance

Open the DLUMA page in Editor options and choose **System**, **Light**, or **Dark** under **Theme**. System follows the Windows app appearance. Changes apply while the Editor is open and also update the visual quest editor.

Windows High Contrast colors take precedence over the selected theme. On older Windows versions, the title bar may continue to use the Windows frame appearance.

The appearance adapters support the 64-bit Developer Tools Editor labeled **6.2022.0317.2106**, including its bundled QuestEditor, MEControl, Scintilla 3.0.4, and MFC 10 components. The supported binary identities are listed in the [native profile](../src/dlw_editor_theme_profile.c). Replaced component binaries can retain their existing colors. Quest source keeps the Editor's Consolas 10 default and native font preferences.

When a custom startup image is included, it runs on its own UI thread. The lower-left corner shows the DLUMA version and commit, the Editor build, and startup progress. DLUMA hides the native splash when it loads; the original image can appear briefly before then. See [Custom startup image](../assets/editor-splash/README.md) for local build instructions.

About uses the same transparent animation, with copyright and version information at the lower left and a centered OK button. Press Enter or Escape to close it.

The Source/Graph window integration is described in [Editor panels](EDITOR_PANELS.md).
