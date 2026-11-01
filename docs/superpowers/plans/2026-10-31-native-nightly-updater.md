# Native Nightly Updater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Check for updates and show the Yes/No prompt inside DLUMA without shipping the 73.6 MB self-contained .NET updater.

**Architecture:** A bounded native worker fetches and validates the official release manifest, compares it with the installed build, and publishes a copied offer for the existing XUI prompt. Acceptance downloads and verifies the package; a small packaged PowerShell script applies that staged package only after the matching Player exits. Retain the current stable backup and restoration contract.

**Tech Stack:** C, WinHTTP, BCrypt SHA-256, existing XUI and JSON reader, Windows PowerShell 5.1, existing package/transaction checks.

**Spec:** `docs/superpowers/specs/2026-10-31-native-nightly-updater.md`

## Global Constraints

- Remove the self-contained .NET updater executable.
- Checking, comparison, downloading and offer presentation belong to the native loader.
- The after-exit script performs no network requests.
- Preserve existing mod settings, mod data, stable-backup/restore behavior and package validation.
- No machine-specific paths, credentials, or separate .NET installation are required.
- Preserve public DLUMA API/ABI 1.0 and the user's nightly preference.

## Review Focus

- Offline, timeout and HTTP errors leave the game playable and produce a useful status.
- Manifest/ZIP mismatch or a changed nightly release cannot install a different offer silently.
- Decline, shutdown, or cancelled requests cannot launch an installation accidentally.
- PID reuse and a still-running Player cannot permit overwriting its module.
- Interrupted installation and mutable runtime folders retain rollback and saved state.

## Task 1: Native check and main-menu offer

**Files:** Modify `src/dlw_nightly.c`, `src/dlw_nightly.h`, existing nightly popup handling in `src/dlw_ui.c`, custom-menu composition in `src/dlw_custom_main_menu.c`, `DLUMA.vcxproj`. Create `src/dlw_nightly_download.c/.h` and `tests/nightly_offer_core_harness.c`.

**Interfaces:** Retain `dlw_nightly_initialize(int)`, `dlw_nightly_set_enabled(int)`, `dlw_nightly_get_offer(void)`, `dlw_nightly_respond(int)` and `dlw_nightly_shutdown(void)`. Add `int dlw_nightly_download_start(const WCHAR *url, const WCHAR *destination, unsigned long long byte_limit)` and `int dlw_nightly_download_poll(void)` internally; poll returns 0 pending, 1 complete, -1 failed. Only one request may be active. Shutdown closes the worker and WinHTTP handles.

- [ ] Add core offer checks for older/same/new commit identities, disabled preference, decline, malformed manifest and errors; reuse existing updater manifest cases. Assert no offer for identical or ancestor remote commits, and one offer per newer manifest per session.
- [ ] Replace external `probe` launch with native asynchronous WinHTTP GET of the existing official `nightly.json` URL. Require HTTPS, normal certificate validation, bounded redirects to GitHub asset hosts, a 1 MiB manifest limit and finite connection/read timeouts. Reuse existing JSON parsing and commit/hash validation.
- [ ] Compare against the recorded installed nightly commit, falling back to the embedded build commit for an installation without nightly state. Store the exact manifest digest in the offer. Never treat a missing local state file as proof that the current build is old. Use the GitHub compare endpoint for the official repository and offer only an ahead result; behind/identical results cannot offer a downgrade, and diverged/unknown comparisons report that automatic comparison is unavailable.
- [ ] Keep the current XUI Yes/No popup and main-menu stability gate. Add the loader-owned nightly controls during custom-menu composition when absent, using the bundled XUI controls as the authoritative template; bump the overlay cache schema so older custom menus receive them without being republished. Never duplicate controls already present. Expose Checking, Up to date, Update available or Check failed through the existing settings/status surface; log the result without private paths or credentials. Verify slow/offline requests do not block rendering or menu input. Add one generic custom-menu fixture lacking nightly controls; assert composition inserts one complete popup and recomposition does not duplicate it.
- [ ] Run the focused offer checks and existing updater tests, then commit the native check/prompt change.

## Task 2: Verified staging and after-exit application

**Files:** Modify `src/dlw_nightly.c`, `DLUMA.vcxproj`, `tools/build_managed_runtime.py`, `tools/verify_package_manifest.py` and existing updater/package tests. Create `tools/apply_nightly_update.ps1`. Use `tools/Dluma.NightlyUpdater/Program.cs`, `InstallLock.cs` and `TransactionChain.cs` as the existing behavioral reference; inspect their current file names before implementation.

**Interfaces:** The packaged `apply_nightly_update.ps1` takes typed `ModulePath`, `StageDirectory`, `ManifestSha256`, `PlayerPid`, `PlayerStartTicks` and `Action` parameters. Action is Update or Restore. The native loader launches it with `-NoProfile -NonInteractive -WindowStyle Hidden -File`; never generate inline script from input.

- [ ] Acceptance downloads the ZIP into the private staging area, checks the accepted manifest digest and ZIP SHA-256, and validates its inventory/path rules. A changed remote offer requires a fresh prompt. Denial performs no download or installation.
- [ ] Port the current install lock, exact-process wait, verified payload allowlist, stable backup and rollback steps into the apply script. Reject traversal, reparse escapes, partial stages and an unexpected process start time. Keep settings, saves, other mods and mutable runtime data outside replacement operations.
- [ ] Exercise success, corrupt stage, process still alive, PID reuse, interrupted replacement and restore using the existing generic updater fixtures. Confirm a failed transaction leaves the previous working package recoverable.
- [ ] Remove the .NET updater publish/copy target and its runtime package requirement. Update package manifests/deployment consumers together to ship the small script. Keep the legacy C# implementation as a reference until migration checks pass, then remove obsolete runtime code and update instructions.
- [ ] Build from a clean source export; verify the package contains no `dluma_nightly_updater.exe`, requires no extra runtime, and the script is included. Commit the migration.

## Task 3: Release and bounded Player verification

**Files:** Update `docs/USING_DLUMA.md` and `.github/workflows/windows-ci.yml` only where the current package behavior requires it.

- [ ] Run the native build, existing package/export checks and focused updater tests. Publish a real newer nightly through the existing authorized GitHub workflow and require the exact commit's jobs to finish successfully.
- [ ] On an older disposable installation, enable updates, reach the main menu and verify the in-game Yes/No offer. Decline once and confirm no files change. Accept on a later session, close Player, verify installation and restart into the offered build.
- [ ] Verify disabling nightly offers/restores the retained stable package, settings and other mods remain available, and offline startup still reaches the menu. Document the actual observed results and commit final documentation.

## Execution handoff

Observed current defects: the selected custom menu has no nightly popup controls, and a remote ancestor commit is treated as an update when the installed-nightly state is absent. The existing helper is published self-contained/single-file and measures 73,606,931 bytes.

Review this plan before implementing the updater replacement. Recommended execution is native implementation in this chat, followed by one focused review; the current release commits and Actions verification can proceed independently.
