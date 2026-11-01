from pathlib import Path

import pytest
from tools.prepare_release import refresh_workshop


def test_workshop_refresh_keeps_open_directory_and_upload_metadata(tmp_path, monkeypatch):
    package = tmp_path / "package"
    package.mkdir()
    (package / "loader.mod").write_bytes(b"old")
    output = tmp_path / "release"
    destination = refresh_workshop(package, output)
    mods = destination / "content/mods"
    (destination / "workshop.vdf").write_text("upload metadata")
    (mods / "stale.mod").write_bytes(b"stale")
    (package / "loader.mod").write_bytes(b"new")
    original = Path.replace

    def locked_directory(source, target):
        if source.is_dir():
            raise PermissionError("Directory has an open Windows handle")
        return original(source, target)

    monkeypatch.setattr(Path, "replace", locked_directory)
    refresh_workshop(package, output)
    assert (mods / "loader.mod").read_bytes() == b"new"
    assert not (mods / "stale.mod").exists()
    assert (destination / "workshop.vdf").read_text() == "upload metadata"


@pytest.mark.parametrize("failure", ["replace", "interrupt", "remove"])
def test_workshop_refresh_restores_payload_on_failure(tmp_path, monkeypatch, failure):
    package = tmp_path / "package"
    package.mkdir()
    (package / "a.mod").write_bytes(b"old a")
    (package / "b.mod").write_bytes(b"old b")
    output = tmp_path / "release"
    destination = refresh_workshop(package, output)
    mods = destination / "content" / "mods"
    (destination / "workshop.vdf").write_text("metadata")
    (mods / "a-stale.mod").write_bytes(b"stale a")
    (mods / "stale.mod").write_bytes(b"stale")
    (package / "a.mod").write_bytes(b"new a")
    (package / "b.mod").write_bytes(b"new b")
    (package / "new").mkdir()
    (package / "new" / "new.mod").write_bytes(b"new")
    original_replace = Path.replace
    original_unlink = Path.unlink

    def failing_replace(source, target):
        if failure in ("replace", "interrupt") and source.name == "b.mod":
            if failure == "interrupt":
                raise KeyboardInterrupt()
            raise PermissionError("locked file")
        return original_replace(source, target)

    def failing_unlink(path, *args, **kwargs):
        if failure == "remove" and path == mods / "stale.mod":
            raise PermissionError("locked file")
        return original_unlink(path, *args, **kwargs)

    monkeypatch.setattr(Path, "replace", failing_replace)
    monkeypatch.setattr(Path, "unlink", failing_unlink)
    with pytest.raises(KeyboardInterrupt if failure == "interrupt" else PermissionError):
        refresh_workshop(package, output)
    assert (mods / "a.mod").read_bytes() == b"old a"
    assert (mods / "b.mod").read_bytes() == b"old b"
    assert (mods / "a-stale.mod").read_bytes() == b"stale a"
    assert (mods / "stale.mod").read_bytes() == b"stale"
    assert not (mods / "new").exists()
    assert (destination / "workshop.vdf").read_text() == "metadata"


def test_workshop_refresh_keeps_backup_when_rollback_fails(tmp_path, monkeypatch):
    package = tmp_path / "package"
    package.mkdir()
    (package / "a.mod").write_bytes(b"old a")
    (package / "b.mod").write_bytes(b"old b")
    output = tmp_path / "release"
    destination = refresh_workshop(package, output)
    (package / "a.mod").write_bytes(b"new a")
    (package / "b.mod").write_bytes(b"new b")
    original_replace = Path.replace

    def failing_replace(source, target):
        if source.name in ("b.mod", "rollback-file"):
            raise PermissionError("locked file")
        return original_replace(source, target)

    monkeypatch.setattr(Path, "replace", failing_replace)
    with pytest.raises(OSError, match="previous payload retained at") as error:
        refresh_workshop(package, output)
    backups = list(destination.glob(".dluma-refresh-*/previous-mods"))
    assert len(backups) == 1
    assert str(backups[0]) in str(error.value)
    assert (backups[0] / "a.mod").read_bytes() == b"old a"
    assert (backups[0] / "b.mod").read_bytes() == b"old b"
