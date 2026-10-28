#!/usr/bin/env python3
"""Create a validated DLUMA release archive and optional Workshop staging tree."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def loader_version() -> str:
    header = (ROOT / "src" / "dlw_common.h").read_text(encoding="utf-8")
    match = re.search(r'^\s*#define\s+DLUMA_VERSION\s+"([0-9]+(?:\.[0-9]+)+)"', header, re.MULTILINE)
    if not match:
        raise ValueError("DLUMA_VERSION is missing from the loader header")
    return match.group(1)


def package_files(package: Path) -> list[Path]:
    paths = sorted(package.rglob("*"), key=lambda path: path.relative_to(package).as_posix().casefold())
    if any(path.is_symlink() for path in paths):
        raise ValueError("release packages must not contain symbolic links")
    return [path for path in paths if path.is_file() and not is_readme(path.name)]


def write_archive(package: Path, output: Path, files: list[Path]) -> None:
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in files:
                entry = zipfile.ZipInfo(path.relative_to(package).as_posix(), (2000, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, path.read_bytes())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def vdf_value(value: object) -> str:
    text = str(value)
    if "\0" in text:
        raise ValueError("Workshop metadata must not contain NUL characters")
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "").replace("\n", "\\n") + '"'


def is_readme(name: str) -> bool:
    return name.casefold() == "readme" or name.casefold().startswith("readme.")


def ignore_readmes(directory: str, names: list[str]) -> set[str]:
    return {name for name in names if is_readme(name)}


def refresh_workshop(package: Path, output: Path) -> Path | None:
    """Refresh only the generated payload, retaining upload metadata and preview."""
    destination = output / "workshop"
    if not destination.exists() and not destination.is_symlink():
        output.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".dluma-workshop-", dir=output) as temporary:
            staged = Path(temporary) / "workshop"
            shutil.copytree(package, staged / "content" / "mods", ignore=ignore_readmes)
            (staged / ".dluma-staging").write_text("DLUMA Workshop release staging\n", encoding="utf-8")
            staged.replace(destination)
        return destination
    marker = destination / ".dluma-staging"
    if (destination.is_symlink() or not destination.is_dir() or marker.is_symlink()
            or not marker.is_file() or marker.read_text(encoding="utf-8") != "DLUMA Workshop release staging\n"):
        raise ValueError("Workshop output already exists and was not created by this tool")
    content = destination / "content"
    mods = content / "mods"
    if content.is_symlink() or mods.is_symlink() or not content.is_dir():
        raise ValueError("Workshop content must be a normal directory")
    temporary = Path(tempfile.mkdtemp(prefix=".dluma-refresh-", dir=destination))
    preserve_backup = False
    try:
        staged = temporary / "mods"
        backup = temporary / "previous-mods"
        shutil.copytree(package, staged, ignore=ignore_readmes)
        if mods.exists():
            if not mods.is_dir():
                raise ValueError("Workshop mods destination must be a directory")
            mods.replace(backup)
        try:
            staged.replace(mods)
        except OSError as error:
            if backup.exists():
                try:
                    backup.replace(mods)
                except OSError as rollback_error:
                    preserve_backup = True
                    raise OSError(f"Workshop refresh failed and the previous payload could not be restored. "
                                  f"Previous payload retained at {backup}: {rollback_error}") from error
            raise
    finally:
        if not preserve_backup:
            shutil.rmtree(temporary)
    return destination


def stage_workshop(package: Path, output: Path, config_path: Path) -> Path:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("Workshop configuration must be a JSON object")
    published_id = str(config.get("publishedfileid", "0"))
    if not published_id.isascii() or not published_id.isdigit():
        raise ValueError("publishedfileid must be a numeric Workshop item ID or 0")
    visibility_names = {"public": 0, "friends": 1, "private": 2, "unlisted": 3}
    visibility = config.get("visibility", "private")
    if not isinstance(visibility, str) or visibility not in visibility_names:
        raise ValueError("visibility must be public, friends, private, or unlisted")
    for key in ("title", "description", "changenote"):
        if not isinstance(config.get(key, ""), str):
            raise ValueError(f"{key} must be text")
    preview = config.get("previewfile", "")
    if not isinstance(preview, str):
        raise ValueError("previewfile must be a path or an empty string")
    preview_path = (config_path.parent / preview).resolve() if preview else None
    if preview_path is not None and not preview_path.is_file():
        raise ValueError(f"Workshop preview file does not exist: {preview_path}")

    destination = output / "workshop"
    if destination.is_symlink():
        raise ValueError("Workshop staging destination must not be a symbolic link")
    marker_name = ".dluma-staging"
    marker_text = "DLUMA Workshop release staging\n"
    if destination.exists():
        marker = destination / marker_name
        if not marker.is_file() or marker.read_text(encoding="utf-8") != marker_text:
            raise ValueError("Workshop output already exists and was not created by this tool; choose another output directory")
    with tempfile.TemporaryDirectory(prefix=".dluma-workshop-", dir=output) as temporary:
        staged = Path(temporary) / "workshop"
        shutil.copytree(package, staged / "content" / "mods", ignore=ignore_readmes)
        (staged / marker_name).write_text(marker_text, encoding="utf-8")
        metadata = {
            "appid": "239140",
            "publishedfileid": published_id,
            "contentfolder": (destination / "content").as_posix(),
            "visibility": visibility_names[visibility],
            "title": config.get("title", "DLUMA"),
            "description": config.get("description", ""),
            "changenote": config.get("changenote", ""),
        }
        if preview_path is not None:
            shutil.copy2(preview_path, staged / ("preview" + preview_path.suffix))
            metadata["previewfile"] = (destination / ("preview" + preview_path.suffix)).as_posix()
        vdf = '"workshopitem"\n{\n' + "".join(f"    {vdf_value(key)} {vdf_value(value)}\n" for key, value in metadata.items()) + "}\n"
        (staged / "workshop.vdf").write_text(vdf, encoding="utf-8", newline="\n")
        if destination.exists():
            # This directory is generated exclusively by this release tool.
            if destination.resolve().parent != output.resolve():
                raise ValueError("Workshop staging destination escaped the release directory")
            shutil.rmtree(destination)
        staged.replace(destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=Path, default=ROOT / "build" / "package" / "Release")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "release")
    parser.add_argument("--workshop-config", type=Path, help="optional local Workshop metadata JSON")
    parser.add_argument("--refresh-workshop", action="store_true", help="create or refresh the Workshop staging payload only")
    args = parser.parse_args()
    package = args.package_dir.resolve()
    output = args.output_dir.resolve()
    if output == package or output.is_relative_to(package):
        parser.error("release output must be outside the package directory")
    try:
        if args.refresh_workshop and args.workshop_config:
            parser.error("--refresh-workshop cannot be combined with --workshop-config")
        version = loader_version()
        mod_name = f"DLUMA ({version}).mod"
        subprocess.run([sys.executable, str(ROOT / "tools" / "verify_package_manifest.py"), "--package-dir", str(package), "--mod-name", mod_name], check=True)
        files = package_files(package)
        if args.refresh_workshop:
            staging = refresh_workshop(package, output)
            if staging:
                print(f"Refreshed Workshop content: {staging}")
            return 0
        output.mkdir(parents=True, exist_ok=True)
        archive = output / f"DLUMA-{version}.zip"
        write_archive(package, archive, files)
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        (output / "SHA256SUMS").write_text(f"{digest}  {archive.name}\n", encoding="ascii", newline="\n")
        print(f"Created {archive.name}: {digest}")
        if args.workshop_config:
            staging = stage_workshop(package, output, args.workshop_config.resolve())
            print(f"Prepared Workshop content and metadata: {staging}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
