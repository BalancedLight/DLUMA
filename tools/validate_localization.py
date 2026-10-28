#!/usr/bin/env python3
"""Validate every loader localization catalog and its packaging contract."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
import re
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCALE_ROOT = ROOT / "PakAssets" / "Locale"
PACK = ROOT / "build" / "package" / "Release" / "dluma.pak"
SUPPORTED = ("En", "De", "Fr", "It", "Es", "Ru", "Jp", "Pl", "Nl", "Br", "Ko", "Cn", "Tw", "El", "Tr", "Th", "Cs")
PROTECTED_TOKEN_PATTERNS = {
    "placeholder": re.compile(r"\{[A-Za-z_][A-Za-z0-9_]*\}"),
    "color": re.compile(r"%COLOR\([^)]*\)"),
    "localized token": re.compile(r"&[A-Za-z0-9_]+&"),
    "permission": re.compile(
        r"(?<![A-Za-z0-9_])(?:game\.read|player\.read|world\.query|"
        r"events\.sensor|events\.interact|storage\.mod|ui\.debug|"
        r"ui\.overlay|audio\.play|world\.write)(?![A-Za-z0-9_])"
    ),
    "path": re.compile(r"(?:[A-Za-z0-9_.-]+\\)+(?:[A-Za-z0-9_-]+)"),
    "configuration file": re.compile(
        r"(?<![A-Za-z0-9_])dluma\.ini(?![A-Za-z0-9_])"
    ),
    "control": re.compile(
        r"(?<![A-Za-z0-9_])(?:F8|Q|E)(?![A-Za-z0-9_])"
    ),
}
UNTRANSLATED_ALLOWLIST = {
    "MODS", "Mods", "WORKSHOP", "Workshop", "LUA", "Lua", "DETAILS", "Details",
    "VERSION", "Version", "STATUS", "Status", "SOURCE", "Source", "LOCAL", "Local",
    "GENERAL", "General", "DESCRIPTION", "Description", "ID", "SHA-256", "DLL", "PE",
    "Runtime", "Exception", "Risk", "Name", "No", "DLUMA", "DLUMA {version}", "DLUMA v{version}",
}
SHARED_LOCALIZED_KEYS = {
    ("Pl", "DLUMA_ValueEditor_Shift"),
    ("Nl", "DLUMA_ValueEditor_Shift"),
    ("Nl", "DLUMA_ValueEditor_Letters"),
    ("Nl", "DLUMA_ValueEditor_Backspace"),
    ("Br", "DLUMA_ValueEditor_Shift"),
    ("Cs", "DLUMA_ValueEditor_Shift"),
}
FORBIDDEN_TRANSLATED_FRAGMENTS = ("review required",)

sys.path.insert(0, str(ROOT / "tools"))
from compile_localized_scr import ScrError, compile_file, parse_scr  # noqa: E402
from decompile_localized_bin import decompile_values  # noqa: E402


def has_wrapping_quotes(value: str) -> bool:
    return len(value) >= 2 and value.startswith('"') and value.endswith('"')


def has_terminal_punctuation(value: str) -> bool:
    if has_wrapping_quotes(value):
        value = value[1:-1]
    return value.endswith((".", "!", "?", "。", "！", "？", "…", ";"))


def delimiter_shape(value: str) -> tuple[int, int, int]:
    return (
        value.count("(") + value.count("（"),
        value.count(")") + value.count("）"),
        value.count(" / "),
    )


def translation_merge_errors(
    code: str,
    english: dict[str, str],
    values: dict[str, str],
    keys: list[str],
    family: str,
) -> list[str]:
    groups: dict[str, list[str]] = defaultdict(list)
    for key in keys:
        if key in english and key in values:
            groups[values[key]].append(key)
    errors: list[str] = []
    for keys_with_value in groups.values():
        if len({english[key] for key in keys_with_value}) > 1:
            errors.append(
                f"{code}: {family} translations collapse distinct English values: "
                + ", ".join(sorted(keys_with_value))
            )
    return errors


def validate(check_pack: bool = True) -> list[str]:
    errors: list[str] = []
    manifest_path = LOCALE_ROOT / "locale_manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover - diagnostic path
        errors.append(f"manifest: {exc}")
        manifest = {}
    if tuple(item.get("code") for item in manifest.get("locales", [])) != SUPPORTED:
        errors.append("manifest: locale order/count does not match supported 17-code list")

    catalogs: dict[str, dict[str, str]] = {}
    with tempfile.TemporaryDirectory(prefix="dluma-localization-") as temp:
        temp_path = Path(temp)
        for code in SUPPORTED:
            source = LOCALE_ROOT / code / "loader_texts_all.scr"
            if not source.is_file():
                errors.append(f"{code}: missing SCR")
                continue
            try:
                values = parse_scr(source)
                compiled = temp_path / f"{code}.bin"
                compile_file(source, compiled)
                if decompile_values(compiled.read_bytes()) != values:
                    errors.append(f"{code}: SCR/BIN round-trip mismatch")
                catalogs[code] = values
            except (OSError, ScrError, UnicodeError) as exc:
                errors.append(f"{code}: {exc}")
    if "En" not in catalogs:
        return errors
    english = catalogs["En"]
    for code, values in catalogs.items():
        missing = sorted(set(english) - set(values))
        extra = sorted(set(values) - set(english))
        if missing:
            errors.append(f"{code}: missing keys: {', '.join(missing)}")
        if extra:
            errors.append(f"{code}: extra keys: {', '.join(extra)}")
        for key in set(english).intersection(values):
            for token_name, pattern in PROTECTED_TOKEN_PATTERNS.items():
                if pattern.findall(english[key]) != pattern.findall(values[key]):
                    errors.append(f"{code}: {token_name} mismatch for {key}")
            if has_wrapping_quotes(english[key]) != has_wrapping_quotes(values[key]):
                errors.append(f"{code}: wrapping quote mismatch for {key}")
            if code != "Th" and has_terminal_punctuation(english[key]) and not has_terminal_punctuation(values[key]):
                errors.append(f"{code}: terminal punctuation mismatch for {key}")
            english_shape = delimiter_shape(english[key])
            translated_shape = delimiter_shape(values[key])
            if any(
                expected and expected != actual
                for expected, actual in zip(
                    english_shape, translated_shape, strict=True
                )
            ):
                errors.append(f"{code}: delimiter shape mismatch for {key}")
        if code != "En":
            if code == "El":
                greek = [key for key, value in values.items()
                         if re.search(r"[\u0370-\u03ff\u1f00-\u1fff]", value)]
                if greek:
                    errors.append("El: Greek text in Latin American Spanish catalog: "
                                  + ", ".join(sorted(greek)))
            same = [key for key in english if key in values and values[key] == english[key]
                    and values[key] not in UNTRANSLATED_ALLOWLIST
                    and (code, key) not in SHARED_LOCALIZED_KEYS
                    and not re.search(r"(?:SHA|Workshop|Dying Light|DLUMA_|x64|Lua|DLL|INI|PE)", values[key])]
            if same:
                errors.append(f"{code}: untranslated values: {', '.join(same)}")
            for fragment in FORBIDDEN_TRANSLATED_FRAGMENTS:
                affected = [
                    key for key, value in values.items()
                    if fragment.casefold() in value.casefold()
                ]
                if affected:
                    errors.append(
                        f"{code}: untranslated fragment {fragment!r}: "
                        + ", ".join(sorted(affected))
                    )

            setting_labels = [
                key for key in english
                if key.startswith("DLUMA_Settings_") and key.endswith("_Label")
            ]
            setting_hints = [
                key for key in english
                if key.startswith("DLUMA_Settings_") and key.endswith("_Hint")
            ]
            mod_details = [
                key for key in english if key.startswith("DLUMA_ModDetails_")
            ]
            errors.extend(translation_merge_errors(
                code, english, values, setting_labels, "setting label"
            ))
            errors.extend(translation_merge_errors(
                code, english, values, setting_hints, "setting hint"
            ))
            errors.extend(translation_merge_errors(
                code, english, values, mod_details, "mod-detail"
            ))
            errors.extend(translation_merge_errors(
                code,
                english,
                values,
                ["DLUMA_Mods_State_New", "DLUMA_Mods_State_Changed"],
                "review-state",
            ))

    if check_pack and PACK.is_file():
        with zipfile.ZipFile(PACK) as archive:
            names = {name.replace("\\", "/") for name in archive.namelist()}
            expected = {f"data/locale/{code}/loader_texts_all.scr" for code in SUPPORTED}
            missing = sorted(expected - names)
            if missing:
                errors.append(f"PAK: missing locale entries: {', '.join(missing)}")
            for entry in sorted(expected.intersection(names)):
                data = archive.read(entry)
                if not data.startswith(b"\xff\xfe"):
                    errors.append(f"PAK: {entry} is not UTF-16LE with BOM")
                    continue
                try:
                    text = data.decode("utf-16")
                except UnicodeError as exc:
                    errors.append(f"PAK: {entry} is invalid UTF-16: {exc}")
                    continue
                if not text.startswith("!String(s, s)"):
                    errors.append(f"PAK: {entry} does not start with !String(s, s)")
                if "\r\n" not in text:
                    errors.append(f"PAK: {entry} must use CRLF line endings")
                code = entry.split("/")[2]
                source_text = (
                    LOCALE_ROOT / code / "loader_texts_all.scr"
                ).read_text(encoding="utf-8-sig")
                normalized_pack = text.replace("\r\n", "\n").replace("\r", "\n")
                normalized_source = (
                    source_text.replace("\r\n", "\n").replace("\r", "\n")
                )
                if normalized_pack != normalized_source:
                    errors.append(f"PAK: {entry} does not match its SCR source")
            if any(name.endswith("dluma_locales.ini") for name in names):
                errors.append("PAK: old crash locale INI is still packaged")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--skip-pack", action="store_true")
    args = parser.parse_args()
    errors = validate(check_pack=not args.skip_pack)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(f"Localization valid: {len(SUPPORTED)} locales")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
