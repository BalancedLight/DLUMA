# Native nightly updater

The loader checks the official DLUMA nightly release in the background when nightly updates are enabled. Once the main menu is stable, a newer verified build produces the existing in-game Yes/No prompt. Declining makes no installation changes. Network errors are logged and visible as a short status; they do not prevent play.

Remove the self-contained .NET updater executable. Checking, comparison, downloading and offer presentation belong to the native loader. A small, shipped Windows PowerShell script may apply an already downloaded update after the exact Player process exits, because its loaded module cannot be overwritten safely. It performs no network requests. Preserve existing mod settings, mod data, stable-backup/restore behavior and package validation. No machine-specific paths, credentials, or separate .NET installation are required.
