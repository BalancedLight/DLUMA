import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_main_menu_presentation_placeholder_harness() -> None:
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_main_menu_presentation_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )


def test_main_menu_presentation_core_harness() -> None:
    # The executable harness covers bounded expansion and both lower- and
    # upper-case digest validation without a game process.
    subprocess.run(
        [str(ROOT / "build/main-menu-presentation-fake/main_menu_presentation_core_harness.exe")],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [str(ROOT / "build/main-menu-presentation-fake/main_menu_presentation_core_harness.exe")],
        cwd=ROOT,
        check=True,
    )
