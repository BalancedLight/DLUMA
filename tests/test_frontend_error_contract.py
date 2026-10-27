"""Guard the no-native-frontend failure path and Lua retirement ordering."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FrontendErrorContract(unittest.TestCase):
    def test_panel_has_only_explicit_quit_and_no_native_frontend_calls(self):
        text = (ROOT / 'src/dlw_frontend_error.c').read_text()
        present = text[text.index('int dluma_frontend_error_present('):text.index('static void error_diagnostics')]
        self.assertNotIn('QUIT_MESSAGE', present)
        self.assertNotIn('schedule_orderly_close', text)
        self.assertNotIn('TerminateProcess', text)
        self.assertNotIn('ExitProcess', text)
        self.assertNotIn('MenuMain', text)
        close = text[text.index('if (message == WM_CLOSE'):text.index('if (message == DLUMA_ERROR_NCDESTROY')]
        self.assertIn('DestroyWindow(window)', close)
        self.assertNotIn('quit', close.lower())
        self.assertIn('DLUMA_FRONTEND_ERROR_QUIT_MESSAGE', text)
        self.assertNotIn('Sleep(', text)
        admission = text[text.index('int dluma_frontend_error_blocks_project_launch('):text.index('void dluma_frontend_error_bind_owner_window(')]
        self.assertIn('g_error_blocked', admission)
        self.assertNotIn('error_try_lock', admission)
        self.assertNotIn('error_try_lock', text)
        self.assertNotIn('dluma_log', present)
        self.assertNotIn('CreateWindow', present)
        self.assertNotIn('while (', present)
        self.assertNotIn('for (', present)
        self.assertIn('PostMessageW', present)
        self.assertLess(present.index('dluma_frontend_error_latch'), present.index('g_error_reason_ready'))
        self.assertEqual(text.count('dluma_frontend_error_latch(&g_error,'),1)
        self.assertNotIn('dluma_frontend_error_note_presentation(&g_error', text)
        owner=text[text.index('void dluma_frontend_error_present_on_owner('):text.index('int dluma_frontend_error_consume_user_quit(')]
        self.assertIn('dluma_log_pair', owner)
        self.assertLess(owner.index('error_is_owner'),owner.index('dluma_log_pair'))

    def test_shutdown_marshals_user_quit_without_entering_native_menu(self):
        text = (ROOT / 'src/dlw_shutdown.c').read_text()
        start = text.index('if (message == DLUMA_FRONTEND_ERROR_QUIT_MESSAGE)')
        end = text.index('dluma_mouse_lock_window_message_pre(', start)
        body = text[start:end]
        self.assertLess(body.index('consume_user_quit'), body.index('dluma_request_quiesce'))
        self.assertIn('user_frontend_error_quit', body)
        self.assertIn('CallWindowProcW(original, window, WM_CLOSE', body)

    def test_project_lua_drain_requires_affinity_generation_and_no_callbacks(self):
        text = (ROOT / 'src/dlw_lua_runtime.c').read_text()
        start = text.index('int dluma_lua_runtime_revoke_local_scopes_for_exit(')
        end = text.index('static int dluma_map_exit_key_equal', start)
        body = text[start:end]
        destroy = body.index('dluma_lua_runtime_destroy_slot(slot, 0, 0)')
        for prerequisite in ['dluma_project_exit_validate_authority',
                             'expected_generation != result.observed_generation',
                             'status.active_callbacks']:
            self.assertLess(body.index(prerequisite), destroy)
        self.assertGreater(body.index('api_owner.game_thread_id=current_thread'), body.index('if (result.active_callbacks)'))
        self.assertNotIn('dispatch_slot(', body)
        self.assertIn('local_script_destruction_deferred', body)
        self.assertIn('g_dluma_lua_project_exit_gate', text[text.index('static int dluma_lua_runtime_create_slot('):])

    def test_post_destroy_finish_cannot_execute_user_notifications(self):
        text=(ROOT/'src/dlw_lua_runtime.c').read_text()
        start=text.index('int dluma_project_runtime_finish_outgoing_map(')
        end=text.index('int dluma_project_runtime_resume_frontend(',start)
        finish=text[start:end]
        self.assertNotIn('dispatch_slot(',finish)
        self.assertNotIn('apply_context(',finish)
        self.assertNotIn('publish_context(',finish)
        self.assertIn('begin_complete',finish)
        self.assertIn('dluma_project_exit_validate_authority',finish)
        self.assertIn('current_project_root[0]=0',finish)
        self.assertIn('pending_context_sequence',finish)
        self.assertLess(finish.index('dluma_map_exit_key_equal'),finish.index('dluma_project_exit_validate_authority'))
        self.assertLess(finish.index('dluma_project_exit_validate_authority'),finish.index('context_rebound){success=1'))
        key=text[text.index('static int dluma_map_exit_key_equal'):text.index('int dluma_project_runtime_snapshot_outgoing_map')]
        self.assertIn('a->serialized_route_token==b->serialized_route_token',key)

if __name__ == '__main__': unittest.main()
