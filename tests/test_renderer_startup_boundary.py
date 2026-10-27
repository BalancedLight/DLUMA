from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_renderer_boundary_never_restarts_player_or_file_observation():
    folder = ROOT / "src/fixes/custom_map_asset_isolation"
    boundary = (folder / "RendererStartupIntegration.inl").read_text()
    assert "FinishDeferredRendererObservation()" in boundary
    assert "Initialize(context,true)" not in boundary
    production = (folder / "CustomMapAssetIsolationFix.cpp").read_text()
    start = production.index("FixStatus Initialize(const RuntimeContext&")
    initialize = production[start:production.index("FixStatus Shutdown()", start)]
    assert "return ArmRendererStartup" not in initialize
    assert initialize.index("PrepareNativeFileBridge();") < initialize.index("InstallMandatoryRoutes()")
    assert initialize.index("InstallMandatoryRoutes()") < initialize.index("ArmRendererStartup(context)")
    integration = (folder / "NativeResourceProductionIntegration.inl").read_text()
    deferred = integration[integration.index("bool FinishDeferredRendererObservation()"):]
    deferred = deferred[:deferred.index("cold::DomainResult ColdResourcesBaseline")]
    assert "InitializeRenderer" in deferred
    assert "resourceprod::Initialize(" not in deferred
    assert "PrepareNativeFileBridge" not in deferred


def test_renderer_startup_boundary_owns_only_the_exact_first_lookup():
    output = ROOT / "build/renderer-startup-fake"
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmd.exe", "/d", "/c",
                    r"call tools\setup_msvc.bat && cl.exe /nologo /W4 /WX /O2 /EHs-c- /std:c++17 /I src "
                    r"tests\renderer_startup_fake_harness.cpp "
                    r"/Fobuild\renderer-startup-fake\harness.obj "
                    r"/Febuild\renderer-startup-fake\harness.exe"], cwd=ROOT, check=True)
    subprocess.run([str(output / "harness.exe")], cwd=ROOT, check=True)
