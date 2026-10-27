# Contributing to DLUMA

Build DLUMA using [the source-build instructions](docs/BUILDING.md), then keep your change focused on the behavior you want to improve.

## Reporting an issue

Include the DLUMA version, whether you were using Player or Developer Tools, the steps that reproduce the problem, and the installed mods involved. Attach the relevant log excerpt or crash report after removing personal paths and account information. A short reproduction project is helpful when you can share it.

## Making a change

- Match the surrounding code and keep unrelated changes out of the pull request.
- Keep generated API files in sync by changing the catalog or generator and running `python tools/generate_api.py`.
- Preserve package identities and API record layouts unless the change explicitly requires a versioned migration.
- Put installation paths and other local settings in ignored configuration. Use portable sample data in public code and examples.
- Include only artwork, audio, source, and dependencies that may be redistributed.
- Use clear, direct wording in menus and documentation. Explain the effect of a setting and when it applies.

Build the affected components and run the existing core checks relevant to the change. UI changes should be checked in the menu with mouse, keyboard, and controller where applicable. Add a test when it protects core behavior; avoid tests that depend on exact prose, private fixtures, or a developer's machine.

## Pull requests

Describe the problem, what the change does, and how you checked it. Mention any required restart, package migration, or supported-game-build change. Screenshots are useful for menu changes.

Contributions to project-owned source and examples are made under the repository's MIT license. Keep existing third-party notices with their files.
