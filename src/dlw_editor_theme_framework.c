#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define COBJMACROS
#include <windows.h>
#include <oleauto.h>
#include <stdint.h>
#include <limits.h>
#include "dlw_editor_theme_framework.h"
#include "dlw_editor_theme_profile.h"
extern void dluma_log_pair(const char *, const char *, const char *);

/* Published CLR4 hosting interfaces. Unused members stay opaque; the loader
 * never starts a CLR or changes the existing hosting configuration. */
static const GUID meta_class = {0x9280188d,0x0e8e,0x4867,{0xb3,0x0c,0x7f,0xa8,0x38,0x84,0xe8,0xde}};
static const GUID meta_id = {0xd332db9e,0xb9b3,0x4125,{0x82,0x07,0xa1,0x48,0x84,0xf5,0x32,0x16}};
static const GUID info_id = {0xbd39d1d2,0xba2f,0x486a,{0x89,0xb0,0xb4,0xb0,0xcb,0x46,0x68,0x91}};
static const GUID legacy_class = {0xcb2f6723,0xab3a,0x11d2,{0x9c,0x40,0x00,0xc0,0x4f,0xa3,0x0a,0x3e}};
static const GUID legacy_id = {0xcb2f6722,0xab3a,0x11d2,{0x9c,0x40,0x00,0xc0,0x4f,0xa3,0x0a,0x3e}};
static const GUID host_class = {0x90f1a06e,0x7712,0x4762,{0x86,0xb5,0x7a,0x5e,0xba,0x6b,0xdb,0x02}};
static const GUID host_id = {0x90f1a06c,0x7712,0x4762,{0x86,0xb5,0x7a,0x5e,0xba,0x6b,0xdb,0x02}};
static const GUID assembly_id = {0x17156360,0x2f1a,0x384a,{0xbc,0x52,0xfd,0xe9,0x3c,0x21,0x5c,0x5b}};
static const GUID null_id = {0};

typedef HRESULT (WINAPI *CreateMetaFn)(REFCLSID, REFIID, LPVOID *);
typedef HRESULT (WINAPI *EnumLoadedFn)(IUnknown *, HANDLE, IEnumUnknown **);
typedef HRESULT (WINAPI *GetVersionFn)(IUnknown *, WCHAR *, DWORD *);
typedef HRESULT (WINAPI *GetDirectoryFn)(IUnknown *, WCHAR *, DWORD *);
typedef HRESULT (WINAPI *GetInterfaceFn)(IUnknown *, REFCLSID, REFIID, LPVOID *);
typedef HRESULT (WINAPI *IsStartedFn)(IUnknown *, BOOL *, DWORD *);
typedef HRESULT (WINAPI *DefaultDomainFn)(IUnknown *, IUnknown **);
typedef HRESULT (WINAPI *ExecuteFn)(IUnknown *, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, DWORD *);
typedef HRESULT (WINAPI *GetAssembliesFn)(IUnknown *, SAFEARRAY **);
typedef HRESULT (WINAPI *LoadTypeLibFn)(LPCOLESTR, REGKIND, ITypeLib **);
typedef void (WINAPI *FreeStringFn)(BSTR);
typedef UINT (WINAPI *StringLengthFn)(BSTR);
typedef HRESULT (WINAPI *ClearVariantFn)(VARIANTARG *);
typedef UINT (WINAPI *ArrayDimFn)(SAFEARRAY *);
typedef HRESULT (WINAPI *ArrayBoundFn)(SAFEARRAY *, UINT, LONG *);
typedef HRESULT (WINAPI *ArrayTypeFn)(SAFEARRAY *, VARTYPE *);
typedef HRESULT (WINAPI *ArrayElementFn)(SAFEARRAY *, LONG *, void *);
typedef HRESULT (WINAPI *ArrayDestroyFn)(SAFEARRAY *);

static struct {
    volatile LONG busy, stopping, admission, pending_refresh;
    volatile LONG environment, applied_environment;
    IUnknown *runtime_host;
    HMODULE automation;
    LoadTypeLibFn load_types;
    FreeStringFn free_string;
    StringLengthFn string_length;
    ClearVariantFn clear_variant;
    ArrayDimFn array_dim;
    ArrayBoundFn array_lower, array_upper;
    ArrayTypeFn array_type;
    ArrayElementFn array_element;
    ArrayDestroyFn array_destroy;
    WCHAR assembly_path[32768];
    uint64_t generation;
    int initialized, pending;
} framework;

static const WCHAR root_property[] = L"DLUMA.EditorTheme.FrameworkRoot.1";

static void *com_method(IUnknown *object, unsigned int index)
{
    return object ? ((void **)object->lpVtbl)[index] : NULL;
}

static int append(WCHAR *out, unsigned int capacity, const WCHAR *text)
{
    unsigned int used = 0, i = 0;
    while (used < capacity && out[used]) ++used;
    while (text[i]) {
        if (used + 1U >= capacity) return 0;
        out[used++] = text[i++];
    }
    out[used] = 0;
    return 1;
}

static int automation_load(void)
{
    HMODULE library = LoadLibraryExW(L"oleaut32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return 0;
    framework.automation = library;
#define AUTOMATION(member, type, name) framework.member = (type)GetProcAddress(library, name)
    AUTOMATION(load_types, LoadTypeLibFn, "LoadTypeLibEx");
    AUTOMATION(free_string, FreeStringFn, "SysFreeString");
    AUTOMATION(string_length, StringLengthFn, "SysStringLen");
    AUTOMATION(clear_variant, ClearVariantFn, "VariantClear");
    AUTOMATION(array_dim, ArrayDimFn, "SafeArrayGetDim");
    AUTOMATION(array_lower, ArrayBoundFn, "SafeArrayGetLBound");
    AUTOMATION(array_upper, ArrayBoundFn, "SafeArrayGetUBound");
    AUTOMATION(array_type, ArrayTypeFn, "SafeArrayGetVartype");
    AUTOMATION(array_element, ArrayElementFn, "SafeArrayGetElement");
    AUTOMATION(array_destroy, ArrayDestroyFn, "SafeArrayDestroy");
#undef AUTOMATION
    return framework.load_types && framework.free_string && framework.string_length && framework.clear_variant &&
        framework.array_dim && framework.array_lower && framework.array_upper &&
        framework.array_type && framework.array_element && framework.array_destroy;
}

static int get_property(IDispatch *assembly, WCHAR *name, VARIANT *value)
{
    DISPID identifier;
    DISPPARAMS parameters;
    EXCEPINFO exception;
    UINT error = 0;
    HRESULT result;
    ZeroMemory(&parameters, sizeof(parameters));
    ZeroMemory(&exception, sizeof(exception));
    ZeroMemory(value, sizeof(*value));
    if (FAILED(IDispatch_GetIDsOfNames(assembly, &null_id, &name, 1,
                                       LOCALE_INVARIANT, &identifier))) return 0;
    result = IDispatch_Invoke(assembly, identifier, &null_id, LOCALE_INVARIANT,
        DISPATCH_PROPERTYGET, &parameters, value, &exception, &error);
    if (exception.bstrSource) framework.free_string(exception.bstrSource);
    if (exception.bstrDescription) framework.free_string(exception.bstrDescription);
    if (exception.bstrHelpFile) framework.free_string(exception.bstrHelpFile);
    return SUCCEEDED(result) && V_VT(value) == VT_BSTR && V_BSTR(value);
}

static int is_mecontrol_assembly(IUnknown *assembly, const WCHAR *module_path)
{
    static const WCHAR identity[] = L"MEControl_x64_rwdi, Version=1.0.8111.38148,";
    IDispatch *dispatch = NULL;
    VARIANT full_name, location;
    int matches = 0;
    ZeroMemory(&full_name, sizeof(full_name)); ZeroMemory(&location, sizeof(location));
    if (FAILED(IUnknown_QueryInterface(assembly, &assembly_id, (void **)&dispatch))) return 0;
    if (get_property(dispatch, L"FullName", &full_name) &&
        framework.string_length(V_BSTR(&full_name)) >= sizeof(identity) / sizeof(WCHAR) - 1U &&
        CompareStringOrdinal(V_BSTR(&full_name), (int)(sizeof(identity) / sizeof(WCHAR) - 1U),
                             identity, (int)(sizeof(identity) / sizeof(WCHAR) - 1U), TRUE) == CSTR_EQUAL &&
        get_property(dispatch, L"Location", &location) &&
        !lstrcmpiW(V_BSTR(&location), module_path)) matches = 1;
    framework.clear_variant(&location); framework.clear_variant(&full_name);
    IDispatch_Release(dispatch);
    return matches;
}

static int default_domain_owns_mecontrol(IUnknown *legacy, const WCHAR *directory)
{
    WCHAR path[32768], type_name[32] = L"_AppDomain";
    WCHAR module_path[32768];
    HMODULE control = GetModuleHandleW(L"MEControl_x64_rwdi.dll");
    ITypeLib *library = NULL;
    ITypeInfo *type = NULL;
    IUnknown *unknown = NULL, *domain = NULL;
    TYPEATTR *attribute = NULL;
    GUID domain_id;
    MEMBERID member;
    USHORT found = 1;
    UINT function_index, function_count = 0;
    unsigned int assembly_slot = 0;
    SAFEARRAY *assemblies = NULL;
    LONG lower = 0, upper = -1, index;
    VARTYPE element_type = VT_EMPTY;
    int admitted = 0;
    DWORD length;
    ZeroMemory(path, sizeof(path));
    if (!control || !append(path, 32768, directory) ||
        !append(path, 32768, L"mscorlib.tlb")) goto done;
    length = GetModuleFileNameW(control, module_path, 32768);
    if (!length || length >= 32768 ||
        FAILED(framework.load_types(path, REGKIND_NONE, &library)) ||
        FAILED(ITypeLib_FindName(library, type_name, 0, &type, &member, &found)) ||
        !found || !type || FAILED(ITypeInfo_GetTypeAttr(type, &attribute))) goto done;
    domain_id = attribute->guid;
    function_count = attribute->cFuncs;
    if (attribute->typekind != TKIND_INTERFACE || function_count > 128) goto done;
    for (function_index = 0; function_index < function_count; ++function_index) {
        FUNCDESC *descriptor = NULL;
        BSTR name = NULL;
        UINT name_count = 0;
        if (FAILED(ITypeInfo_GetFuncDesc(type, function_index, &descriptor))) break;
        if (SUCCEEDED(ITypeInfo_GetNames(type, descriptor->memid, &name, 1, &name_count)) &&
            name_count && name && !lstrcmpW(name, L"GetAssemblies") &&
            descriptor->funckind == FUNC_PUREVIRTUAL && descriptor->callconv == CC_STDCALL &&
            descriptor->invkind == INVOKE_FUNC && descriptor->cParams == 1 &&
            descriptor->cParamsOpt == 0 && descriptor->elemdescFunc.tdesc.vt == VT_HRESULT &&
            descriptor->lprgelemdescParam[0].tdesc.vt == VT_PTR &&
            descriptor->lprgelemdescParam[0].tdesc.lptdesc &&
            descriptor->lprgelemdescParam[0].tdesc.lptdesc->vt == VT_SAFEARRAY &&
            (descriptor->lprgelemdescParam[0].paramdesc.wParamFlags &
             (PARAMFLAG_FOUT | PARAMFLAG_FRETVAL)) == (PARAMFLAG_FOUT | PARAMFLAG_FRETVAL) &&
            descriptor->oVft >= 3 * (int)sizeof(void *) &&
            descriptor->oVft % sizeof(void *) == 0 &&
            descriptor->oVft + sizeof(void *) <= attribute->cbSizeVft)
            assembly_slot = (unsigned int)descriptor->oVft / (unsigned int)sizeof(void *);
        if (name) framework.free_string(name);
        ITypeInfo_ReleaseFuncDesc(type, descriptor);
        if (assembly_slot) break;
    }
    if (!assembly_slot ||
        FAILED(((DefaultDomainFn)com_method(legacy, 13))(legacy, &unknown)) || !unknown ||
        FAILED(IUnknown_QueryInterface(unknown, &domain_id, (void **)&domain)) || !domain ||
        FAILED(((GetAssembliesFn)com_method(domain, assembly_slot))(domain, &assemblies)) ||
        !assemblies || framework.array_dim(assemblies) != 1 ||
        FAILED(framework.array_lower(assemblies, 1, &lower)) ||
        FAILED(framework.array_upper(assemblies, 1, &upper)) ||
        upper < lower || (uint64_t)((int64_t)upper - lower) >= 512U ||
        FAILED(framework.array_type(assemblies, &element_type)) ||
        (element_type != VT_UNKNOWN && element_type != VT_DISPATCH)) goto done;
    for (index = lower; index <= upper; ++index) {
        IUnknown *assembly = NULL;
        if (SUCCEEDED(framework.array_element(assemblies, &index, &assembly)) && assembly) {
            admitted = is_mecontrol_assembly(assembly, module_path);
            IUnknown_Release(assembly);
            if (admitted) break;
        }
        if (index == LONG_MAX) break;
    }
done:
    if (assemblies) framework.array_destroy(assemblies);
    if (domain) IUnknown_Release(domain);
    if (unknown) IUnknown_Release(unknown);
    if (attribute) ITypeInfo_ReleaseTypeAttr(type, attribute);
    if (type) ITypeInfo_Release(type);
    if (library) ITypeLib_Release(library);
    return admitted;
}

static int framework_admit(void)
{
    HMODULE shim;
    CreateMetaFn create;
    IUnknown *meta = NULL, *legacy = NULL, *candidate = NULL;
    IEnumUnknown *runtimes = NULL;
    WCHAR directory[32768], version[64];
    HRESULT result;
    int admitted = 0;
    if (framework.admission) return framework.admission == 1;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_FRAMEWORK)) return 0;
    framework.admission = -1;
    shim = GetModuleHandleW(L"mscoree.dll");
    create = shim ? (CreateMetaFn)GetProcAddress(shim, "CLRCreateInstance") : NULL;
    if (!create || !automation_load() ||
        FAILED(create(&meta_class, &meta_id, (void **)&meta)) || !meta ||
        FAILED(((EnumLoadedFn)com_method(meta, 6))(meta, GetCurrentProcess(), &runtimes))) goto done;
    while (IEnumUnknown_Next(runtimes, 1, &candidate, NULL) == S_OK) {
        IUnknown *info = NULL;
        DWORD count = 64, flags = 0;
        BOOL started = FALSE;
        if (SUCCEEDED(IUnknown_QueryInterface(candidate, &info_id, (void **)&info))) {
            result = ((GetVersionFn)com_method(info, 3))(info, version, &count);
            if (SUCCEEDED(result) && !lstrcmpW(version, L"v4.0.30319") &&
                SUCCEEDED(((IsStartedFn)com_method(info, 14))(info, &started, &flags)) && started) {
                count = 32768;
                if (SUCCEEDED(((GetDirectoryFn)com_method(info, 4))(info, directory, &count)) &&
                    SUCCEEDED(((GetInterfaceFn)com_method(info, 9))(info, &legacy_class, &legacy_id, (void **)&legacy)) &&
                    legacy && default_domain_owns_mecontrol(legacy, directory) &&
                    SUCCEEDED(((GetInterfaceFn)com_method(info, 9))(info, &host_class, &host_id,
                                                                   (void **)&framework.runtime_host))) admitted = 1;
            }
            IUnknown_Release(info);
        }
        IUnknown_Release(candidate); candidate = NULL;
        if (admitted) break;
    }
    if (admitted) {
        HMODULE self;
        DWORD length, i;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)(ULONG_PTR)framework_admit, &self)) admitted = 0;
        else {
            length = GetModuleFileNameW(self, framework.assembly_path, 32768);
            if (!length || length >= 32768) admitted = 0;
            else {
                for (i = length; i && framework.assembly_path[i - 1U] != L'\\' &&
                                      framework.assembly_path[i - 1U] != L'/'; --i) { }
                framework.assembly_path[i] = 0;
                if (!i || !append(framework.assembly_path, 32768,
                    L"dluma_runtime\\editor\\Dluma.EditorTheme.Framework.dll")) admitted = 0;
            }
        }
    }
done:
    if (legacy) IUnknown_Release(legacy);
    if (candidate) IUnknown_Release(candidate);
    if (runtimes) IEnumUnknown_Release(runtimes);
    if (meta) IUnknown_Release(meta);
    if (!admitted && framework.runtime_host) {
        IUnknown_Release(framework.runtime_host); framework.runtime_host = NULL;
    }
    framework.admission = admitted ? 1 : -1;
    dluma_log_pair(admitted ? "INFO" : "WARN", "editor_theme_framework_domain",
                   admitted ? "default_domain_mecontrol_verified" : "default_domain_unavailable");
    return admitted;
}

static void decimal(WCHAR *out, unsigned int *used, uint64_t value)
{
    WCHAR reverse[20];
    unsigned int count = 0;
    do { reverse[count++] = (WCHAR)(L'0' + value % 10U); value /= 10U; } while (value);
    while (count) out[(*used)++] = reverse[--count];
}

static void hexadecimal(WCHAR *out, unsigned int *used, uint64_t value, unsigned int digits)
{
    static const WCHAR symbols[] = L"0123456789ABCDEF";
    while (digits) { --digits; out[(*used)++] = symbols[(value >> (digits * 4U)) & 15U]; }
}

static int invoke(const WCHAR *method, HWND root, const DlumaEditorThemeResolved *theme)
{
    WCHAR argument[512];
    unsigned int used = 0, i;
    DWORD returned = 0;
    HRESULT result;
    argument[used++] = L'1';
    if (theme) {
        argument[used++] = L'|'; decimal(argument, &used, theme->generation);
        argument[used++] = L'|'; decimal(argument, &used, theme->kind);
        argument[used++] = L'|'; decimal(argument, &used, theme->preference);
        argument[used++] = L'|'; hexadecimal(argument, &used, (uint64_t)(ULONG_PTR)root, 16);
        argument[used++] = L'|';
        for (i = 0; i < 17; ++i) {
            if (i) argument[used++] = L',';
            hexadecimal(argument, &used, theme->argb[i], 8);
        }
    }
    argument[used] = 0;
    result = ((ExecuteFn)com_method(framework.runtime_host, 11))(
        framework.runtime_host, framework.assembly_path,
        L"Dluma.EditorTheme.Framework.ThemeHost", method, argument, &returned);
    return SUCCEEDED(result) ? (int)returned : -1;
}

static int managed_root(HWND window)
{
    WCHAR name[128], parent_name[128];
    HWND parent;
    DWORD process_id = 0;
    if (GetWindowThreadProcessId(window, &process_id) != GetCurrentThreadId() ||
        process_id != GetCurrentProcessId() ||
        GetClassNameW(window, name, 128) < 14 ||
        CompareStringOrdinal(name, 14, L"WindowsForms10", 14, TRUE) != CSTR_EQUAL) return 0;
    parent = GetParent(window);
    return !parent || GetClassNameW(parent, parent_name, 128) < 14 ||
        CompareStringOrdinal(parent_name, 14, L"WindowsForms10", 14, TRUE) != CSTR_EQUAL;
}

int dluma_editor_theme_framework_apply(HWND window, const DlumaEditorThemeResolved *theme)
{
    int result = 0, enrolled;
    LONG environment;
    if (!theme || theme->size != sizeof(*theme) || theme->version != 1U ||
        framework.stopping || !managed_root(window)) return 0;
    if (InterlockedCompareExchange(&framework.busy, 1, 0)) {
        InterlockedExchange(&framework.pending_refresh, 1); return 0;
    }
    if (!framework_admit()) goto done;
    environment = InterlockedCompareExchange(&framework.environment, 0, 0);
    enrolled = GetPropW(window, root_property) != NULL;
    if (!enrolled) {
        result = invoke(framework.initialized ? L"Apply" : L"Initialize", window, theme);
        if (result >= 0) {
            framework.initialized = 1;
            (void)SetPropW(window, root_property, (HANDLE)1);
        }
    } else result = 0;
    if (result >= 0 && (framework.generation != theme->generation ||
                       framework.applied_environment != environment)) {
        result = invoke(L"Apply", NULL, theme);
        if (result >= 0) {
            framework.generation = theme->generation;
            framework.applied_environment = environment;
        }
    }
    framework.pending = result == 1;
done:
    InterlockedExchange(&framework.busy, 0);
    if (InterlockedExchange(&framework.pending_refresh, 0))
        (void)dluma_editor_ui_request_refresh();
    return result >= 0 && framework.initialized;
}

int dluma_editor_theme_framework_stop(void)
{
    int result;
    InterlockedExchange(&framework.stopping, 1);
    if (InterlockedCompareExchange(&framework.busy, 1, 0)) return 0;
    if (framework.initialized && framework.runtime_host) {
        result = invoke(L"Shutdown", NULL, NULL);
        if (result != 0) { framework.pending = 1; InterlockedExchange(&framework.busy, 0); return 0; }
    }
    framework.initialized = framework.pending = 0;
    if (framework.runtime_host) {
        IUnknown_Release(framework.runtime_host); framework.runtime_host = NULL;
    }
    if (framework.automation) { FreeLibrary(framework.automation); framework.automation = NULL; }
    InterlockedExchange(&framework.busy, 0);
    return 1;
}

int dluma_editor_theme_framework_callbacks_drained(void)
{
    return framework.stopping && !framework.busy && !framework.initialized && !framework.pending;
}

void dluma_editor_theme_framework_settings_changed(void)
{
    InterlockedIncrement(&framework.environment);
}
