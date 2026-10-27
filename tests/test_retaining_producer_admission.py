"""Rejecting an observed producer must precede its native side effects."""
from test_content_ledger_observer_tokens import SOURCE, function


def test_shared_wrapper_requires_actual_replay_identity_and_has_startup_passthrough():
    wrapper = SOURCE[SOURCE.index("class RetainingObservationCall {"):SOURCE.index("class ActiveConsumerCall {")]
    assert "g_player_reset_replay_depth" not in wrapper
    assert "!g_cold_reboot_registry.sealed" in wrapper
    assert "g_content_live" in wrapper
    assert "cold::CurrentRegistryReplayPermit" in wrapper
    assert "if (replay_active || observed)" in wrapper
    assert "replay_active && observed && observed->domain == domain" in wrapper
    assert "cold::BeginRetainingReplayObservation" in wrapper
    assert "replay_permit.operations" not in wrapper
    assert "g_cold_reboot_registry.phase" not in wrapper
    assert "dluma_frontend_error_present" in wrapper


def test_audio_rejected_producers_use_verified_native_failure_contracts():
    cases = [
        ("HookAudioScriptLoad", "false", "g_original_audio_script_load"),
        ("HookToolsLoadAudioBanks", "false", "g_original_tools_load_audio_banks"),
        ("HookAudioFmodLoadBanks", "-1", "g_original_audio_fmod_load_banks"),
        ("HookSoundBankManagerLoad", "-1", "g_original_sound_bank_manager_load"),
        ("HookAudioVoiceFactory", "nullptr", "g_original_audio_voice_factory"),
        ("HookAudioNamedExecute", "1", "g_original_audio_named_execute"),
        ("HookToolsAudioScriptExecute", "1", "g_original_tools_audio_script_execute"),
    ]
    for name, failure, original in cases:
        body = function(name)
        rejection = f"if (!retaining.admitted()) return {failure};"
        assert rejection in body
        assert body.index(rejection) < body.index(original + "(")
        assert body.count(original + "(") == 1


def test_native_xui_loads_are_independent_retaining_consumers():
    for suffix in ("XmlLoad", "BinaryPackLoad", "BinaryFileLoad"):
        body = function("HookXui" + suffix)
        assert "cold::ContentDomainId::NativeFrontendEntries" in body
        assert body.index("if (!retaining.admitted()) return 0;") < body.index("g_original_xui_")
        assert body.index("if (!retaining.admitted()) return 0;") < body.index("XuiContentLoadScope input_scope")
        assert body.index("XuiContentLoadScope input_scope") < body.index("g_original_xui_")
        assert body.index("g_original_xui_") < body.index("input_scope.Finish(")
        assert "ObservePotentialNativeDomainWrite" not in body


def test_native_writer_touch_is_reconciled_from_independent_evidence():
    write = function("ObservePotentialNativeDomainWrite")
    reconcile = function("SealCandidateRetainingConsumersAtResetBoundary")
    assert "_InterlockedOr(&g_native_observed_domain_touch_mask" in write
    assert "&g_native_observed_domain_touch_mask" in reconcile
    assert "authoritative_untouched_mask &= ~independent_authoritative_touch_mask" in reconcile
