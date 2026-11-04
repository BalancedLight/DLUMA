#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma warning(push)
#pragma warning(disable: 4201) /* Windows SDK Rich Edit anonymous unions. */
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#pragma warning(pop)
#include "dlw_editor_theme_hints.h"
extern "C" void dluma_log_pair(const char *, const char *, const char *);

static bool hints_owner(HWND window, void *owner)
{
    return GetPropW(window, L"DLUMA.EditorTheme.Control.1") == owner ||
           GetPropW(window, L"DLUMA.EditorTheme.Control.Detaching.1") == owner;
}

extern "C" void *dluma_editor_theme_hints_freeze(HWND window, void *owner)
{
    IRichEditOle *ole = nullptr;
    ITextDocument *document = nullptr;
    long count = 0;
    if (!hints_owner(window, owner) ||
        !SendMessageW(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole)) || !ole)
        return nullptr;
    HRESULT hr = ole->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void **>(&document));
    ole->Release();
    if (FAILED(hr) || !document) return nullptr;
    if (!hints_owner(window, owner) || FAILED(document->Freeze(&count))) {
        document->Release();
        return nullptr;
    }
    return document;
}

extern "C" void dluma_editor_theme_hints_unfreeze(void *token, int destroying)
{
    ITextDocument *document = static_cast<ITextDocument *>(token);
    if (!document) return;
    if (!destroying) {
        long count = 0;
        document->Unfreeze(&count);
    }
    document->Release();
}

extern "C" int dluma_editor_theme_hints_foreground(HWND window, void *owner, COLORREF expected,
                                                     int expected_auto, COLORREF desired,
                                                     int desired_auto)
{
    IRichEditOle *ole = nullptr;
    ITextDocument *document = nullptr;
    ITextRange *range = nullptr;
    ITextFont *font = nullptr;
    long current = tomUndefined;
    HRESULT hr = E_FAIL, resumed = E_FAIL;
    int changed = 0;
    CHARRANGE selection = {}, after = {};
    POINT scroll = {}, after_scroll = {};
    if (!hints_owner(window, owner)) return 0;
    LRESULT modified = SendMessageW(window, EM_GETMODIFY, 0, 0);
    if (!hints_owner(window, owner)) return 0;
    SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (!hints_owner(window, owner)) return 0;
    SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    if (!hints_owner(window, owner)) return 0;
    if (!SendMessageW(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole)) || !ole)
        return 0;
    hr = ole->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void **>(&document));
    if (SUCCEEDED(hr) && document) hr = document->Range(0, tomForward, &range);
    if (SUCCEEDED(hr) && range) hr = range->GetFont(&font);
    if (SUCCEEDED(hr) && font) hr = font->GetForeColor(&current);
    if (SUCCEEDED(hr) && font && hints_owner(window, owner) &&
        current == (expected_auto ? tomAutoColor : static_cast<long>(expected))) {
        hr = document->Undo(tomSuspend, nullptr);
        if (SUCCEEDED(hr)) {
            CHARFORMAT2A format = {};
            format.cbSize = sizeof(format);
            format.dwMask = CFM_COLOR;
            format.dwEffects = desired_auto ? CFE_AUTOCOLOR : 0;
            format.crTextColor = desired;
            if (hints_owner(window, owner) &&
                SendMessageA(window, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format))) {
                long actual = tomUndefined;
                if (hints_owner(window, owner) && SUCCEEDED(font->GetForeColor(&actual)) &&
                    actual == (desired_auto ? tomAutoColor : static_cast<long>(desired))) changed = 1;
                else if (hints_owner(window, owner))
                    dluma_log_pair("WARN", "editor_hints_theme", "foreground_readback_failed");
            }
            resumed = document->Undo(tomResume, nullptr);
            if (FAILED(resumed)) {
                long actual = tomUndefined;
                int restored = 0;
                dluma_log_pair("WARN", "editor_hints_theme", "undo_resume_failed");
                if (changed && hints_owner(window, owner) && SUCCEEDED(font->GetForeColor(&actual)) &&
                    actual == (desired_auto ? tomAutoColor : static_cast<long>(desired))) {
                    format.dwEffects = expected_auto ? CFE_AUTOCOLOR : 0;
                    format.crTextColor = expected;
                    if (SendMessageA(window, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format)) &&
                        hints_owner(window, owner) && SUCCEEDED(font->GetForeColor(&actual)) &&
                        actual == (expected_auto ? tomAutoColor : static_cast<long>(expected))) restored = 1;
                }
                if (!restored && changed) dluma_log_pair("WARN", "editor_hints_theme", "foreground_rollback_unconfirmed");
                if (FAILED(document->Undo(tomResume, nullptr)))
                    dluma_log_pair("WARN", "editor_hints_theme", "undo_resume_retry_failed");
                if (restored) changed = 0;
                else if (changed) {
                    actual = tomUndefined;
                    if (hints_owner(window, owner) && SUCCEEDED(font->GetForeColor(&actual)) &&
                        actual == (desired_auto ? tomAutoColor : static_cast<long>(desired))) changed = 2;
                    else changed = 0;
                }
            }
        }
    }
    if (font) font->Release();
    if (range) range->Release();
    if (document) document->Release();
    ole->Release();
    if (!hints_owner(window, owner)) return 0;
    SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
    if (!hints_owner(window, owner)) return 0;
    if (selection.cpMin != after.cpMin || selection.cpMax != after.cpMax)
        SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after_scroll));
    if (!hints_owner(window, owner)) return 0;
    if (scroll.x != after_scroll.x || scroll.y != after_scroll.y)
        SendMessageW(window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    if (!hints_owner(window, owner)) return 0;
    if (SendMessageW(window, EM_GETMODIFY, 0, 0) != modified &&
        hints_owner(window, owner))
        SendMessageW(window, EM_SETMODIFY, modified, 0);
    return changed;
}
