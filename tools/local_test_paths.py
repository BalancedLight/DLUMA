from __future__ import annotations

import json
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "tests" / "local_paths.json"


def _load_config() -> dict[str, str]:
    if not CONFIG_PATH.is_file():
        return {}
    data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{CONFIG_PATH} must contain a JSON object")
    return {
        str(key): value.strip()
        for key, value in data.items()
        if isinstance(value, str) and value.strip()
    }


_CONFIG = _load_config()


def _configured_path(key: str, environment: str) -> Path | None:
    value = os.environ.get(environment) or _CONFIG.get(key)
    return Path(value).expanduser() if value else None


DEVTOOLS_ROOT = _configured_path("devtools_root", "DLW_TEST_DEVTOOLS_ROOT")
if DEVTOOLS_ROOT is None:
    DEVTOOLS_ROOT = ROOT / ".local-path-unconfigured" / "devtools_root"

DEVTOOLS_DEFAULT_PROJECT = _configured_path(
    "devtools_default_project", "DLW_TEST_DEVTOOLS_DEFAULT_PROJECT"
)
if DEVTOOLS_DEFAULT_PROJECT is None:
    DEVTOOLS_DEFAULT_PROJECT = DEVTOOLS_ROOT / "workshop" / "DevToolsDefaultProject"

MENU_OVERRIDE_PROJECT = _configured_path(
    "menu_override_project", "DLW_TEST_MENU_OVERRIDE_PROJECT"
)
if MENU_OVERRIDE_PROJECT is None:
    MENU_OVERRIDE_PROJECT = DEVTOOLS_ROOT / "workshop" / "MenuOverride"
