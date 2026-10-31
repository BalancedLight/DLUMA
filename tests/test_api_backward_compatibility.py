"""Keep published API 1.0 layouts and service ordinals backward compatible."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def contract(value):
    if isinstance(value, dict):
        return {k: contract(v) for k, v in value.items()
                if k not in {"description", "documentation", "notes"}}
    if isinstance(value, list):
        return [contract(v) for v in value]
    return value


def test_published_api_1_0_contract_is_preserved():
    previous = json.loads((ROOT / "api/compatibility/1.0-catalog.json").read_text())
    current = json.loads((ROOT / "api/generated/dluma_api_catalog.json").read_text())
    for key in ("status_values", "thread_affinities", "scopes", "languages", "capabilities", "access"):
        for item in contract(previous[key]):
            assert item in contract(current[key]), (key, item)
    types = {t["name"]: contract(t) for t in current["types"]}
    for old in previous["types"]:
        assert types.get(old["name"]) == contract(old), old["name"]
    domains = {d["domain"]: d for d in current["domains"]}
    for old in previous["domains"]:
        new = domains[old["domain"]]
        assert old["service"] == new["service"]
        operations = {o["id"]: o for o in new["operations"]}
        for op in old["operations"]:
            for key in ("id", "ordinal", "request", "result"):
                assert operations[op["id"]].get(key) == op.get(key), (op["id"], key)
