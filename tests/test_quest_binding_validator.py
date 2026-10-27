import importlib.util
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_quest_bindings", ROOT / "tools" / "verify_quest_bindings.py"
)
assert SPEC and SPEC.loader
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)

BUILDER_SPEC = importlib.util.spec_from_file_location(
    "build_content_pack", ROOT / "tools" / "build_content_pack.py"
)
assert BUILDER_SPEC and BUILDER_SPEC.loader
content_builder = importlib.util.module_from_spec(BUILDER_SPEC)
BUILDER_SPEC.loader.exec_module(content_builder)


def write(path: Path, text: str) -> Path:
    path.write_text(text, encoding="utf-8")
    return path


def test_accepts_unique_authored_quest_and_phase_bindings(tmp_path):
    (tmp_path / "data" / "maps" / "nowhere").mkdir(parents=True)
    xml = write(
        tmp_path / "rescue_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_ReachLab'/><Phase name='DLUMA_ClearLab'/>"
        "</Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "sample_project.rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"one": "One"},
                        "phases": {
                            "reach_lab": "DLUMA_ReachLab",
                            "clear_lab": "DLUMA_ClearLab",
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    validator.validate(
        bindings,
        [xml],
        project_root=tmp_path,
        project="SampleProject",
        map_name="nowhere",
    )


def test_content_pack_preflight_requires_a_complete_custom_map_route(tmp_path):
    project = tmp_path / "SampleProject"
    project.mkdir()
    (project / "data" / "maps" / "nowhere").mkdir(parents=True)
    xml = write(
        project / "rescue_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_ReachLab'/>"
        "</Quest></QuestsDefinitions>",
    )
    bindings = project / "lua_quest_bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "sample_project.rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"reach_lab": "DLUMA_ReachLab"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    content_builder.validate_quest_binding_preflight(
        bindings, [xml], project, "SampleProject", "nowhere"
    )
    content_builder.validate_quest_binding_preflight(
        bindings, [], project, "SampleProject", "nowhere"
    )
    assert validator.main(
        [
            "--bindings", str(bindings),
            "--project-root", str(project),
            "--project", "SampleProject",
            "--map", "nowhere",
        ]
    ) == 0

    with pytest.raises(ValueError, match="--quest-map"):
        content_builder.validate_quest_binding_preflight(
            bindings, [xml], project, "SampleProject", None
        )

    with pytest.raises(ValueError, match="quest binding preflight failed"):
        content_builder.validate_quest_binding_preflight(
            bindings, [xml], project, "SampleProject", "other_map"
        )


def test_project_validation_requires_the_real_map_and_all_quest_xml(tmp_path):
    project = tmp_path / "SampleProject"
    (project / "data" / "maps" / "nowhere").mkdir(parents=True)
    primary = write(
        project / "rescue_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_ReachLab'/>"
        "</Quest></QuestsDefinitions>",
    )
    conflicting = write(
        project / "other.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_Other'/>"
        "</Quest></QuestsDefinitions>",
    )
    bindings = project / "lua_quest_bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "sample_project.rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"reach_lab": "DLUMA_ReachLab"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    with pytest.raises(validator.BindingError, match="was not supplied"):
        validator.validate(
            bindings,
            [primary],
            project_root=project,
            project="SampleProject",
            map_name="nowhere",
        )
    with pytest.raises(validator.BindingError, match="ambiguous"):
        validator.validate(
            bindings,
            [primary, conflicting],
            project_root=project,
            project="SampleProject",
            map_name="nowhere",
        )

    missing_map_project = tmp_path / "NoMap"
    missing_map_project.mkdir()
    missing_xml = write(
        missing_map_project / "rescue_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_ReachLab'/>"
        "</Quest></QuestsDefinitions>",
    )
    missing_bindings = missing_map_project / "bindings.json"
    missing_bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "NoMap",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"reach_lab": "DLUMA_ReachLab"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    with pytest.raises(validator.BindingError, match="map directory does not exist"):
        validator.validate(
            missing_bindings,
            [missing_xml],
            project_root=missing_map_project,
            project="NoMap",
            map_name="nowhere",
        )


def test_rejects_unknown_or_ambiguous_native_targets(tmp_path):
    first = write(
        tmp_path / "one.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    second = write(
        tmp_path / "two.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='Two'/></Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(bindings, [first, second])
    except validator.BindingError as exc:
        assert "ambiguous" in str(exc)
    else:
        raise AssertionError("ambiguous quest target was accepted")


def test_rejects_unknown_or_duplicate_phase_mapping(tmp_path):
    xml = write(
        tmp_path / "quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"a": "One", "b": "One"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "more than once" in str(exc)
    else:
        raise AssertionError("duplicate phase target was accepted")


def test_rejects_two_native_routes_bound_to_the_same_authored_quest(tmp_path):
    xml = write(
        tmp_path / "quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue_a",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"one": "One"},
                    },
                    {
                        "id": "rescue_b",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"one": "One"},
                    },
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "more than one native route" in str(exc)
    else:
        raise AssertionError("duplicate native quest binding was accepted")


def test_rejects_case_insensitive_duplicate_route_ids_and_non_helper_identifiers(tmp_path):
    xml = write(
        tmp_path / "quest.xml",
        "<QuestsDefinitions>"
        "<Quest name='DLUMA_RescueA'><Phase name='One'/></Quest>"
        "<Quest name='DLUMA_RescueB'><Phase name='Two'/></Quest>"
        "</QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "Rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_RescueA",
                        "phases": {"begin": "One"},
                    },
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_RescueB",
                        "phases": {"begin": "Two"},
                    },
                ]
            }
        ),
        encoding="utf-8",
    )
    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "native quest binding id" in str(exc)
    else:
        raise AssertionError("case-insensitive duplicate Lua IDs were accepted")

    for field, value in (
        ("id", "q" * 49),
        ("id", "réscue"),
        ("quest", "q" * 97),
    ):
        bindings.write_text(
            json.dumps(
                {
                    "bindings": [
                        {
                            "id": "rescue",
                            "project": "SampleProject",
                            "map": "nowhere",
                            "quest": "DLUMA_RescueA",
                            "phases": {"begin": "One"},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        document = json.loads(bindings.read_text(encoding="utf-8"))
        document["bindings"][0][field] = value
        bindings.write_text(json.dumps(document), encoding="utf-8")
        try:
            validator.validate(bindings, [xml])
        except validator.BindingError as exc:
            assert "bounded identifier" in str(exc)
        else:
            raise AssertionError(f"invalid {field} was accepted")

    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_RescueA",
                        "phases": {"p" * 65: "One"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "bounded identifier" in str(exc)
    else:
        raise AssertionError("oversized Lua phase ID was accepted")


def test_rejects_an_ambiguous_phase_name_inside_one_native_quest(tmp_path):
    xml = write(
        tmp_path / "quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'>"
        "<Phase name='DLUMA_ReachLab'/><Phase name='DLUMA_ReachLab'/>"
        "</Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {"reach_lab": "DLUMA_ReachLab"},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "ambiguous within" in str(exc)
    else:
        raise AssertionError("ambiguous native phase was accepted")


def test_requires_a_nonempty_phase_mapping_for_native_bindings(tmp_path):
    xml = write(
        tmp_path / "quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                        "phases": {},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(bindings, [xml])
    except validator.BindingError as exc:
        assert "non-empty" in str(exc)
    else:
        raise AssertionError("empty native phase mapping was accepted")


def test_rejects_foreign_project_output_or_wrong_active_route(tmp_path):
    project = tmp_path / "SampleProject"
    project.mkdir()
    xml = write(
        project / "rescue_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Rescue'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    foreign = write(
        tmp_path / "campaign_quest.xml",
        "<QuestsDefinitions><Quest name='DLUMA_Campaign'><Phase name='One'/></Quest></QuestsDefinitions>",
    )
    bindings = tmp_path / "bindings.json"
    bindings.write_text(
        json.dumps(
            {
                "bindings": [
                    {
                        "id": "rescue",
                        "project": "SampleProject",
                        "map": "nowhere",
                        "quest": "DLUMA_Rescue",
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    try:
        validator.validate(
            bindings,
            [foreign],
            project_root=project,
            project="SampleProject",
            map_name="nowhere",
        )
    except validator.BindingError as exc:
        assert "outside custom project root" in str(exc)
    else:
        raise AssertionError("foreign quest output was accepted")

    try:
        validator.validate(
            bindings,
            [xml],
            project_root=project,
            project="SampleProject",
            map_name="another_map",
        )
    except validator.BindingError as exc:
        assert "does not match selected map" in str(exc)
    else:
        raise AssertionError("wrong active map was accepted")
