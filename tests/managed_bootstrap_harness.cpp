#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kAbiVersion = 0x00010000U;
constexpr std::uint32_t kStructVersion = 1U;

struct StringView {
    const char* data;
    std::uint64_t size;
};

struct NativeError;

struct NativeCall {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t status;
    std::uint32_t reserved;
    NativeError* error;
};

struct BridgeV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t abi_version;
    std::uint32_t reserved;
    void* context;
    int(__cdecl* invoke)(
        void*,
        std::uint32_t,
        void*,
        std::uint32_t,
        void*,
        std::uint32_t);
};

struct ArtifactV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t kind;
    std::uint32_t reserved;
    StringView relative_path;
    const unsigned char* bytes;
    std::uint64_t size;
};

struct ModLoadV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint64_t owner_id;
    std::uint32_t scope;
    std::uint32_t map_generation;
    StringView mod_id;
    StringView entry_type;
    StringView entry_path;
    BridgeV1* bridge;
    ArtifactV1* artifacts;
    std::uint32_t artifact_count;
    std::uint32_t reserved;
};

struct ActivationV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t scope;
    std::uint32_t map_generation;
    StringView project;
    StringView map;
};

struct EventV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t kind;
    std::uint32_t map_generation;
    std::uint64_t callback_id;
    std::uint64_t token;
    double number;
    std::int32_t state;
    std::uint32_t reserved;
    StringView name;
    StringView project;
    StringView map;
};

struct BootstrapApiV1 {
    std::uint32_t struct_size;
    std::uint32_t struct_version;
    std::uint32_t abi_version;
    std::uint32_t reserved;
    int(__cdecl* load)(
        ModLoadV1*,
        std::uint64_t*,
        char*,
        std::uint32_t);
    int(__cdecl* activate)(
        std::uint64_t,
        ActivationV1*,
        char*,
        std::uint32_t);
    int(__cdecl* deactivate)(
        std::uint64_t,
        std::uint32_t,
        char*,
        std::uint32_t);
    int(__cdecl* dispatch)(
        std::uint64_t,
        EventV1*,
        char*,
        std::uint32_t);
    int(__cdecl* shutdown)(
        std::uint64_t,
        std::uint32_t,
        char*,
        std::uint32_t);
};

using HostContext = void*;

struct HostFxrParameters {
    std::size_t size;
    const wchar_t* host_path;
    const wchar_t* dotnet_root;
};

using InitializeForRuntimeConfigFn = std::int32_t(__cdecl*)(
    const wchar_t*,
    const HostFxrParameters*,
    HostContext*);
using GetRuntimeDelegateFn = std::int32_t(__cdecl*)(
    HostContext,
    int,
    void**);
using CloseHostContextFn = std::int32_t(__cdecl*)(HostContext);
using LoadAssemblyAndGetFunctionPointerFn = int(__stdcall*)(
    const wchar_t*,
    const wchar_t*,
    const wchar_t*,
    const wchar_t*,
    void*,
    void**);
using GetBootstrapApiFn = int(__cdecl*)(
    std::uint32_t,
    void*,
    std::uint32_t,
    char*,
    std::uint32_t);

struct BridgeState {
    unsigned int log_calls = 0;
};

StringView view(const std::string& value)
{
    return {value.data(), value.size()};
}

int __cdecl invoke(
    void* context,
    std::uint32_t operation_id,
    void*,
    std::uint32_t,
    void* result,
    std::uint32_t result_size)
{
    if (!context || !result ||
        result_size < sizeof(std::uint32_t) * 2U +
            sizeof(NativeCall)) {
        return 14;
    }
    auto* call = reinterpret_cast<NativeCall*>(
        static_cast<unsigned char*>(result) +
        sizeof(std::uint32_t) * 2U);
    call->status = 0;
    if (operation_id == 1001U) {
        auto* state = static_cast<BridgeState*>(context);
        ++state->log_calls;
    }
    return 0;
}

std::vector<unsigned char> read_all(const wchar_t* path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Could not open managed fixture.");
    }
    const auto end = stream.tellg();
    if (end <= 0) {
        throw std::runtime_error("Managed fixture is empty.");
    }
    std::vector<unsigned char> bytes(
        static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("Could not read managed fixture.");
    }
    return bytes;
}

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        require(argc == 5, "Expected dotnet root, hostfxr, bootstrap, and fixture paths.");
        const wchar_t* dotnet_root = argv[1];
        const wchar_t* hostfxr_path = argv[2];
        const wchar_t* bootstrap_path = argv[3];
        const wchar_t* fixture_path = argv[4];

        std::wstring runtime_config = bootstrap_path;
        const auto extension = runtime_config.rfind(L".dll");
        require(extension != std::wstring::npos, "Bootstrap path must end in .dll.");
        runtime_config.replace(
            extension,
            4,
            L".runtimeconfig.json");

        HMODULE hostfxr = LoadLibraryExW(
            hostfxr_path,
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(hostfxr != nullptr, "Could not load hostfxr.");
        auto initialize = reinterpret_cast<InitializeForRuntimeConfigFn>(
            GetProcAddress(
                hostfxr,
                "hostfxr_initialize_for_runtime_config"));
        auto get_delegate = reinterpret_cast<GetRuntimeDelegateFn>(
            GetProcAddress(
                hostfxr,
                "hostfxr_get_runtime_delegate"));
        auto close_context = reinterpret_cast<CloseHostContextFn>(
            GetProcAddress(hostfxr, "hostfxr_close"));
        require(
            initialize && get_delegate && close_context,
            "hostfxr exports are incomplete.");

        HostFxrParameters parameters{
            sizeof(HostFxrParameters),
            argv[0],
            dotnet_root,
        };
        HostContext context = nullptr;
        require(
            initialize(
                runtime_config.c_str(),
                &parameters,
                &context) >= 0 &&
                context,
            "hostfxr initialization failed.");
        void* raw_loader = nullptr;
        require(
            get_delegate(context, 5, &raw_loader) >= 0 &&
                raw_loader,
            "hostfxr component delegate failed.");
        close_context(context);
        auto load_assembly =
            reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(
                raw_loader);
        void* raw_get_api = nullptr;
        require(
            load_assembly(
                bootstrap_path,
                L"Dluma.Managed.Bootstrap.BootstrapEntryPoints, Dluma.Managed.Bootstrap",
                L"GetApi",
                reinterpret_cast<const wchar_t*>(-1),
                nullptr,
                &raw_get_api) >= 0 &&
                raw_get_api,
            "Managed bootstrap entry point failed.");

        auto get_api =
            reinterpret_cast<GetBootstrapApiFn>(raw_get_api);
        BootstrapApiV1 api{};
        char diagnostic[512]{};
        require(
            get_api(
                kAbiVersion,
                &api,
                sizeof(api),
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Bootstrap API negotiation failed.");
        require(
            api.struct_size == sizeof(api) &&
                api.struct_version == kStructVersion &&
                api.abi_version == kAbiVersion &&
                api.load && api.activate && api.deactivate &&
                api.dispatch && api.shutdown,
            "Bootstrap API table is malformed.");

        const auto fixture = read_all(fixture_path);
        const std::string relative_path = "ManagedModFixture.dll";
        const std::string mod_id = "tests.managed.fixture";
        const std::string entry_type = "ManagedModFixture.FixtureMod";
        BridgeState bridge_state{};
        BridgeV1 bridge{
            sizeof(BridgeV1),
            kStructVersion,
            kAbiVersion,
            0,
            &bridge_state,
            invoke,
        };
        ArtifactV1 artifact{
            sizeof(ArtifactV1),
            kStructVersion,
            2,
            0,
            view(relative_path),
            fixture.data(),
            fixture.size(),
        };
        ModLoadV1 load{
            sizeof(ModLoadV1),
            kStructVersion,
            0x4353000000000001ULL,
            1,
            1,
            view(mod_id),
            view(entry_type),
            view(relative_path),
            &bridge,
            &artifact,
            1,
            0,
        };
        std::uint64_t handle = 0;
        diagnostic[0] = 0;
        require(
            api.load(
                &load,
                &handle,
                diagnostic,
                sizeof(diagnostic)) != 0 &&
                handle != 0,
            diagnostic[0] ? diagnostic : "Managed mod load failed.");

        ActivationV1 activation{
            sizeof(ActivationV1),
            kStructVersion,
            1,
            1,
            {},
            {},
        };
        require(
            api.activate(
                handle,
                &activation,
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Managed activation failed.");

        EventV1 update{
            sizeof(EventV1),
            kStructVersion,
            5,
            1,
            0,
            0,
            0.016,
            0,
            0,
            {},
            {},
            {},
        };
        require(
            api.dispatch(
                handle,
                &update,
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Managed update failed.");
        require(
            api.deactivate(
                handle,
                1,
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Managed deactivation failed.");

        require(
            api.activate(
                handle,
                &activation,
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Managed reactivation failed.");
        require(
            api.shutdown(
                handle,
                2,
                diagnostic,
                sizeof(diagnostic)) != 0,
            diagnostic[0] ? diagnostic : "Managed shutdown failed.");
        require(
            bridge_state.log_calls == 5,
            "Lifecycle callbacks did not invoke the shared bridge exactly five times.");

        std::cout
            << "managed bootstrap lifecycle and immutable artifact load passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
