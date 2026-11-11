import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_private_periodic_consent(tmp_path: Path) -> None:
    exe = tmp_path / "boss_consent_tests.exe"
    obj = tmp_path / "boss_consent_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /wd4505 /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_periodic_diagnostic_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss private periodic consent" in run.stdout


def test_factory_late_entry_retention(tmp_path: Path) -> None:
    exe = tmp_path / "boss_factory_late_tests.exe"
    obj = tmp_path / "boss_factory_late_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /wd4505 /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_factory_late_entry_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    for mode in ("empty", "owned"):
        run = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=10)
        assert run.returncode == 0, run.stdout + run.stderr
        assert "PASS: boss factory late entry" in run.stdout


def test_normal_receiver_route(tmp_path: Path) -> None:
    exe = tmp_path / "boss_receiver_tests.exe"
    obj = tmp_path / "boss_receiver_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_receiver_route_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    for mode in ("normal", "protection", "foreign", "update_fault", "original_fault", "concurrent", "publish_stop"):
        run = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=10)
        assert run.returncode == 0, run.stdout + run.stderr
        assert "PASS: boss normal receiver route" in run.stdout


def test_scenario_scheduler(tmp_path: Path) -> None:
    exe = tmp_path / "boss_scheduler_tests.exe"
    obj = tmp_path / "boss_scheduler_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_scheduler_fake_harness.c\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss scheduler" in run.stdout


def test_disabled_callback_orchestration(tmp_path: Path) -> None:
    exe = tmp_path / "boss_callback_tests.exe"
    obj = tmp_path / "boss_callback_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_scheduler_native_fake_harness.c\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: disabled boss callback orchestration" in run.stdout


def test_bounded_template_resolution(tmp_path: Path) -> None:
    exe = tmp_path / "boss_template_tests.exe"
    obj = tmp_path / "boss_template_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_template_resolver_fake_harness.c\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: bounded boss templates" in run.stdout


def test_multi_owner_registry(tmp_path: Path) -> None:
    exe = tmp_path / "boss_registry_tests.exe"
    obj = tmp_path / "boss_registry_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_scheduler_registry_fake_harness.c\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss scenario registry" in run.stdout


def test_real_loader_opcode_factory(tmp_path: Path) -> None:
    exe = tmp_path / "boss_loader_tests.exe"
    obj = tmp_path / "boss_loader_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_actions_loader_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss loader opcodes" in run.stdout


def test_real_loader_periodic_lifecycle(tmp_path: Path) -> None:
    exe = tmp_path / "boss_lifecycle_tests.exe"
    obj = tmp_path / "boss_lifecycle_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_actions_lifecycle_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss loader lifecycle" in run.stdout


def test_periodic_native_effects(tmp_path: Path) -> None:
    exe = tmp_path / "boss_periodic_tests.exe"
    obj = tmp_path / "boss_periodic_tests.obj"
    script = tmp_path / "compile.cmd"
    script.write_text(
        '@echo off\ncall tools\\setup_msvc.bat\nif errorlevel 1 exit /b 1\n'
        f'cl.exe /nologo /TC /MT /W4 /WX /wd4505 /Gy /Fe"{exe}" /Fo"{obj}" '
        'tests\\boss_actions_periodic_native_fake_harness.c /link /OPT:REF kernel32.lib\nexit /b %errorlevel%\n',
        encoding="utf-8",
    )
    built = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=ROOT,
                           capture_output=True, text=True, timeout=90)
    assert built.returncode == 0, built.stdout + built.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "PASS: boss periodic native effects" in run.stdout
