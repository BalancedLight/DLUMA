"""Prepare a repository-local, versioned .NET runtime source for packaging."""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path


def runtime_paths(version: str) -> tuple[str, ...]:
    return (
        "dotnet.exe", "LICENSE.txt", "ThirdPartyNotices.txt",
        f"host/fxr/{version}/hostfxr.dll",
        f"shared/Microsoft.NETCore.App/{version}/coreclr.dll",
        f"shared/Microsoft.NETCore.App/{version}/hostpolicy.dll",
        f"shared/Microsoft.NETCore.App/{version}/System.Private.CoreLib.dll",
        f"shared/Microsoft.WindowsDesktop.App/{version}/PresentationFramework.dll",
    )


def complete(root: Path, version: str) -> bool:
    return all((root / name).is_file() and (root / name).stat().st_size
               for name in runtime_paths(version))


def microsoft_url(url: str) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in {
        "builds.dotnet.microsoft.com", "download.visualstudio.microsoft.com",
        "ci.dot.net", "dotnetcli.azureedge.net",
    }:
        raise ValueError("Unexpected .NET download host")
    return url


def download_runtime(destination: Path, version: str) -> None:
    channel = version.rsplit(".", 1)[0]
    url = f"https://builds.dotnet.microsoft.com/dotnet/release-metadata/{channel}/releases.json"
    with urllib.request.urlopen(url, timeout=60) as response:
        metadata = json.load(response)
    release = next((r for r in metadata["releases"]
                    if r["release-version"] == version), None)
    if release is None:
        raise ValueError(f"Microsoft release metadata does not contain .NET {version}")
    for component in ("runtime", "windowsdesktop"):
        archive_name = ("dotnet-runtime-win-x64.zip" if component == "runtime"
                        else "windowsdesktop-runtime-win-x64.zip")
        record = next((f for f in release[component]["files"]
                       if f["rid"] == "win-x64" and f["name"] == archive_name), None)
        if record is None:
            raise ValueError(f"Missing {component} archive for .NET {version}")
        archive = destination.parent / f"{component}.zip"
        with urllib.request.urlopen(microsoft_url(record["url"]), timeout=60) as response:
            microsoft_url(response.geturl())
            with archive.open("wb") as output:
                shutil.copyfileobj(response, output)
        digest = hashlib.sha512(archive.read_bytes()).hexdigest()
        if digest.lower() != record["hash"].lower():
            raise ValueError(f"Microsoft {component} archive checksum mismatch")
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                name = item.filename.replace("\\", "/")
                target = (destination / name).resolve()
                if not target.is_relative_to(destination.resolve()) or ":" in name:
                    raise ValueError("Unsafe .NET archive path")
                if (item.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError("Symlink in .NET archive")
            source.extractall(destination)


def prepare_runtime(version: str, candidates: list[Path], cache_parent: Path) -> Path:
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid .NET runtime version")
    cache_parent.mkdir(parents=True, exist_ok=True)
    cache = cache_parent / version
    if cache.is_symlink() or not cache.resolve().is_relative_to(cache_parent.resolve()):
        raise ValueError("Runtime cache leaves its build directory")
    if complete(cache, version):
        return cache
    with tempfile.TemporaryDirectory(prefix="prepare-", dir=cache_parent) as temporary:
        source = Path(temporary) / "source"
        source.mkdir()
        installed = next((p for p in candidates if complete(p, version)), None)
        if installed:
            for name in ("dotnet.exe", "LICENSE.txt", "ThirdPartyNotices.txt"):
                shutil.copy2(installed / name, source / name)
            for name in (f"host/fxr/{version}", f"shared/Microsoft.NETCore.App/{version}",
                         f"shared/Microsoft.WindowsDesktop.App/{version}"):
                shutil.copytree(installed / name, source / name)
        else:
            print(f"Restoring Microsoft .NET {version} runtime build cache...", flush=True)
            download_runtime(source, version)
        if not complete(source, version):
            missing = [name for name in runtime_paths(version) if not (source / name).is_file()]
            raise ValueError(f"Prepared .NET {version} runtime is incomplete: {missing}")
        if cache.exists():
            shutil.rmtree(cache)
        source.rename(cache)
    return cache
