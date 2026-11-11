from pathlib import Path
import subprocess


def test_shared_trace_file_bridge_observation_boot(tmp_path):
    root = Path(__file__).resolve().parents[1]
    # Compile the actual startup sealer, including its admission branches.
    owner = (root / 'src/fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp').read_text()
    start = owner.index('bool SealNativeFileBridge() noexcept')
    end = owner.index('\nbool BuildHookPlan()', start)
    (tmp_path / 'native_file_startup_seal_under_test.inl').write_text(owner[start:end])
    sources = ["tests/shared_file_observation_fake_harness.cpp",
               "src/fixes/custom_map_asset_isolation/CustomMapAssetTrace.cpp",
               "src/fixes/custom_map_asset_isolation/NativeFileHookBridge.cpp",
               "src/fixes/custom_map_asset_isolation/NativeFileLifetimeLedger.cpp",
               "src/fixes/custom_map_asset_isolation/NativeFileLifetimeWindows.cpp",
               "src/fixes/custom_map_asset_isolation/ContentDomainRestartRegistry.cpp"]
    script = tmp_path / "build.cmd"
    script.write_text(
        '@echo off\n'
        f'call "{root / "tools/setup_msvc.bat"}" || exit /b 1\n'
        f'ml64.exe /nologo /c /Fo"{tmp_path / "probe.obj"}" '
        f'"{root / "src/native_file_completion_probe_masm.asm"}" || exit /b 1\n'
        'cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 '
        '/D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS '
        f'/I"{root / "src"}" /Fo"{str(tmp_path).replace(chr(92), "/")}/" '
        f'/I"{tmp_path}" '
        f'/Fe"{tmp_path / "observation.exe"}" '
        + ' '.join(f'"{root / item}"' for item in sources)
        + f' /link /stack:16777216 kernel32.lib user32.lib "{tmp_path / "probe.obj"}" || exit /b 1\n'
        f'"{tmp_path / "observation.exe"}" || exit /b 1\n'
    )
    result = subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=tmp_path,
                            check=False, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "shared file observation boot passed" in result.stdout
