from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_native_xui_instance_and_compiled_ownership():
    subprocess.run(["cmd.exe", "/d", "/s", "/c", "tools\\build_native_xui_instance_compiled_fake_tests.bat"], cwd=ROOT, check=True)
    subprocess.run([str(ROOT / "build/native-xui-instance-compiled-fake/native_xui_instance_compiled_fake_harness.exe")], cwd=ROOT, check=True)
