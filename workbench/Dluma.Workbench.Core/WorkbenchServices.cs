using System.Diagnostics;
using System.Security;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace Dluma.Workbench.Core;

public enum ModLanguage
{
    Lua,
    CSharp,
    Native
}

public enum ModScope
{
    Global,
    Project,
    Map
}

public sealed record ApiOperationInfo(
    string Domain,
    string Id,
    uint NumericId,
    string Status,
    string Capability,
    IReadOnlyList<string> Access,
    IReadOnlyList<string> Scopes,
    string Thread,
    string Request,
    string Result,
    string Documentation,
    IReadOnlyDictionary<string, string> Adapters,
    IReadOnlyList<string> Errors);

public sealed record ApiCatalogInfo(
    string Name,
    string Version,
    string CatalogSha256,
    IReadOnlyList<ApiOperationInfo> Operations,
    IReadOnlyList<string> Capabilities,
    IReadOnlyList<string> AccessDeclarations);

public static class RepositoryLocator
{
    public static string Find(string? start = null)
    {
        var current = new DirectoryInfo(start ?? AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "api", "generated", "workbench_api.json")) &&
                File.Exists(Path.Combine(current.FullName, "tools", "validate_mod_package.py")))
                return current.FullName;
            current = current.Parent;
        }
        current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "api", "generated", "workbench_api.json")))
                return current.FullName;
            current = current.Parent;
        }
        throw new DirectoryNotFoundException("Could not locate the DLUMA repository.");
    }
}

public static class ApiCatalogService
{
    public static ApiCatalogInfo Load(string path)
    {
        using var stream = File.OpenRead(path);
        using var document = JsonDocument.Parse(stream);
        var root = document.RootElement;
        var api = root.GetProperty("api");
        var operations = new List<ApiOperationInfo>();
        foreach (var domain in root.GetProperty("domains").EnumerateArray())
        {
            var domainName = domain.GetProperty("domain").GetString() ?? string.Empty;
            foreach (var operation in domain.GetProperty("operations").EnumerateArray())
            {
                var adapters = operation.GetProperty("adapters")
                    .EnumerateObject()
                    .ToDictionary(
                        item => item.Name,
                        item => item.Value.GetString() ?? string.Empty,
                        StringComparer.OrdinalIgnoreCase);
                operations.Add(new ApiOperationInfo(
                    domainName,
                    operation.GetProperty("id").GetString() ?? string.Empty,
                    operation.GetProperty("numeric_id").GetUInt32(),
                    operation.GetProperty("status").GetString() ?? string.Empty,
                    operation.GetProperty("capability").GetString() ?? string.Empty,
                    Strings(operation.GetProperty("access")),
                    Strings(operation.GetProperty("scopes")),
                    operation.GetProperty("thread").GetString() ?? string.Empty,
                    operation.GetProperty("request").GetString() ?? string.Empty,
                    operation.GetProperty("result").GetString() ?? string.Empty,
                    operation.GetProperty("documentation").GetString() ?? string.Empty,
                    adapters,
                    Strings(operation.GetProperty("errors"))));
            }
        }
        operations.Sort((left, right) => left.NumericId.CompareTo(right.NumericId));
        return new ApiCatalogInfo(
            api.GetProperty("name").GetString() ?? "DLUMA API",
            api.GetProperty("version").GetString() ?? "1.0",
            root.GetProperty("catalog_sha256").GetString() ?? string.Empty,
            operations,
            root.GetProperty("capabilities").EnumerateArray()
                .Select(value => value.GetProperty("name").GetString() ?? string.Empty)
                .Where(value => value.Length > 0).ToArray(),
            root.GetProperty("access").EnumerateArray()
                .Select(value => value.GetProperty("name").GetString() ?? string.Empty)
                .Where(value => value.Length > 0).ToArray());
    }

    public static IReadOnlyList<ApiOperationInfo> Filter(
        ApiCatalogInfo catalog,
        string? search,
        string? domain,
        string? status,
        string? scope,
        string? access)
    {
        IEnumerable<ApiOperationInfo> query = catalog.Operations;
        if (!string.IsNullOrWhiteSpace(search))
            query = query.Where(item =>
                item.Id.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                item.Documentation.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                item.Capability.Contains(search, StringComparison.OrdinalIgnoreCase));
        if (!string.IsNullOrWhiteSpace(domain) && domain != "All")
            query = query.Where(item => item.Domain.Equals(domain, StringComparison.OrdinalIgnoreCase));
        if (!string.IsNullOrWhiteSpace(status) && status != "All")
            query = query.Where(item => item.Status.Equals(status, StringComparison.OrdinalIgnoreCase));
        if (!string.IsNullOrWhiteSpace(scope) && scope != "All")
            query = query.Where(item => item.Scopes.Contains(scope, StringComparer.OrdinalIgnoreCase));
        if (!string.IsNullOrWhiteSpace(access) && access != "All")
            query = query.Where(item => item.Access.Contains(access, StringComparer.OrdinalIgnoreCase));
        return query.ToArray();
    }

    private static string[] Strings(JsonElement values) => values
        .EnumerateArray()
        .Select(value => value.GetString() ?? string.Empty)
        .Where(value => value.Length > 0)
        .ToArray();
}

public static class SnippetService
{
    public static string Create(ApiOperationInfo operation, ModLanguage language)
    {
        var key = language switch
        {
            ModLanguage.Lua => "lua",
            ModLanguage.CSharp => "csharp",
            _ => "cpp"
        };
        var adapter = operation.Adapters.TryGetValue(key, out var value)
            ? value
            : operation.Id;
        if (language == ModLanguage.CSharp &&
            adapter.StartsWith("IModContext.", StringComparison.Ordinal))
            adapter = "context." + adapter["IModContext.".Length..];
        var gated = operation.Status == "implemented"
            ? string.Empty
            : $"// {operation.Status.ToUpperInvariant()}: returns structured unavailable until the capability is ready.{Environment.NewLine}";
        return language switch
        {
            ModLanguage.Lua =>
                $"-- {operation.Documentation}{Environment.NewLine}local result, err = {adapter}({{}}){Environment.NewLine}if not result then dluma.log(\"WARN\", err.message) end",
            ModLanguage.CSharp =>
                $"{gated}// {operation.Documentation}{Environment.NewLine}var result = {adapter}();{Environment.NewLine}if (!result.IsSuccess) context.Log.Warn(result.Error.Message);",
            _ =>
                $"{gated}// {operation.Documentation}{Environment.NewLine}auto result = {adapter}({{}});{Environment.NewLine}if (!result) context.log(\"WARN\", result.error().message());"
        };
    }
}

public sealed record ModTemplateRequest(
    string RepositoryRoot,
    string OutputRoot,
    string Id,
    string Name,
    string Version,
    string Description,
    string Author,
    ModLanguage Language,
    ModScope Scope,
    string? MapName,
    string? EntryType,
    int LoadPriority,
    IReadOnlyList<string> Access,
    bool IncludeConfiguration);

public sealed record ModProjectResult(string Directory, string Manifest, string Entry);

public static partial class ModProjectService
{
    [GeneratedRegex("^[a-z0-9][a-z0-9._-]*[a-z0-9]$", RegexOptions.CultureInvariant)]
    private static partial Regex StableIdPattern();

    public static async Task<ModProjectResult> CreateAsync(
        ModTemplateRequest request,
        CancellationToken cancellationToken = default)
    {
        Validate(request);
        var language = LanguageText(request.Language);
        var scope = ScopeText(request.Scope);
        var scopeParts = request.Scope == ModScope.Map
            ? new[] { "maps", request.MapName!, request.Id }
            : new[] { scope, request.Id };
        var prefix = request.Language == ModLanguage.Lua
            ? Path.Combine(request.OutputRoot, "data", "scripts", "lua")
            : Path.Combine(request.OutputRoot, "mods", language);
        var directory = scopeParts.Aggregate(prefix, Path.Combine);
        if (Directory.Exists(directory) && Directory.EnumerateFileSystemEntries(directory).Any())
            throw new IOException($"The target mod directory is not empty: {directory}");
        Directory.CreateDirectory(directory);

        var entry = request.Language switch
        {
            ModLanguage.Lua => "main.lua",
            ModLanguage.CSharp => $"{SafeTypeName(request.Name)}.dll",
            _ => $"{request.Id}.mod"
        };
        var manifest = new JsonObject
        {
            ["$schema"] = "mod.dluma.schema.json",
            ["schema"] = "dluma.mod.v1",
            ["api_version"] = "1.0",
            ["id"] = request.Id,
            ["name"] = request.Name,
            ["version"] = request.Version,
            ["description"] = request.Description,
            ["author"] = request.Author,
            ["language"] = language,
            ["scope"] = scope,
            ["entry"] = entry,
            ["load_priority"] = request.LoadPriority,
            ["access"] = new JsonArray(
                request.Access.Distinct(StringComparer.Ordinal).Order()
                    .Select(value => (JsonNode?)JsonValue.Create(value)).ToArray())
        };
        if (request.Scope == ModScope.Map)
            manifest["map"] = request.MapName;
        if (request.Language == ModLanguage.CSharp)
            manifest["entry_type"] = string.IsNullOrWhiteSpace(request.EntryType)
                ? $"{SafeTypeName(request.Name)}.Mod"
                : request.EntryType;
        if (request.IncludeConfiguration)
            manifest["configuration"] = new JsonObject { ["schema"] = "config.dluma.json" };

        var manifestPath = Path.Combine(directory, "mod.dluma.json");
        await WriteUtf8Async(manifestPath, manifest.ToJsonString(JsonOptions()) + "\n", cancellationToken);
        if (request.IncludeConfiguration)
            await WriteUtf8Async(
                Path.Combine(directory, "config.dluma.json"),
                ConfigurationTemplate(), cancellationToken);

        var entryPath = request.Language switch
        {
            ModLanguage.Lua => await CreateLuaAsync(directory, cancellationToken),
            ModLanguage.CSharp => await CreateCSharpAsync(directory, request, entry, cancellationToken),
            _ => await CreateNativeAsync(directory, request, entry, cancellationToken)
        };
        return new ModProjectResult(directory, manifestPath, entryPath);
    }

    public static void Validate(ModTemplateRequest request)
    {
        if (!StableIdPattern().IsMatch(request.Id) || request.Id.Length is < 3 or > 127)
            throw new ArgumentException("Mod ID must be 3-127 lowercase letters, digits, dots, dashes, or underscores.");
        if (string.IsNullOrWhiteSpace(request.Name) || request.Name.Length > 127)
            throw new ArgumentException("Name is required and must be at most 127 characters.");
        if (!Version.TryParse(request.Version.Split('-', '+')[0], out _))
            throw new ArgumentException("Version must use semantic version form such as 1.0.0.");
        if (string.IsNullOrWhiteSpace(request.Author) || request.Author.Length > 127)
            throw new ArgumentException("Author is required and must be at most 127 characters.");
        if (request.Scope == ModScope.Map && string.IsNullOrWhiteSpace(request.MapName))
            throw new ArgumentException("Map scope requires an exact internal map name.");
        if (request.LoadPriority is < -1000 or > 1000)
            throw new ArgumentOutOfRangeException(nameof(request.LoadPriority));
        if (request.Access.Count > 64 || request.Access.Any(value => string.IsNullOrWhiteSpace(value)))
            throw new ArgumentException("Access declarations must contain at most 64 non-empty IDs.");
    }

    private static async Task<string> CreateLuaAsync(string directory, CancellationToken token)
    {
        var path = Path.Combine(directory, "main.lua");
        const string text = "local mod = dluma.mod()\n\nfunction mod:on_start()\n    dluma.log(\"INFO\", \"DLUMA API 1.0 Lua mod started\")\nend\n\nfunction mod:on_stop(reason)\n    dluma.log(\"INFO\", \"Lua mod stopped: \" .. tostring(reason))\nend\n\nreturn mod\n";
        await WriteUtf8Async(path, text, token);
        return path;
    }

    private static async Task<string> CreateCSharpAsync(
        string directory,
        ModTemplateRequest request,
        string entry,
        CancellationToken token)
    {
        var typeName = SafeTypeName(request.Name);
        var entryType = string.IsNullOrWhiteSpace(request.EntryType)
            ? $"{typeName}.Mod"
            : request.EntryType!;
        var namespaceName = entryType.Contains('.')
            ? entryType[..entryType.LastIndexOf('.')]
            : typeName;
        var className = entryType.Contains('.')
            ? entryType[(entryType.LastIndexOf('.') + 1)..]
            : entryType;
        var sdkProject = Path.Combine(
            request.RepositoryRoot, "sdk", "dotnet", "Dluma.Managed.Sdk", "Dluma.Managed.Sdk.csproj");
        var relativeSdk = Path.GetRelativePath(directory, sdkProject).Replace('\\', '/');
        var intermediateDirectory = Path.Combine(
            request.RepositoryRoot, "build", "workbench-mods", request.Id, "csharp", "obj");
        var relativeIntermediate = Path.GetRelativePath(directory, intermediateDirectory)
            .Replace('\\', '/') + "/";
        var propsPath = Path.Combine(directory, "Directory.Build.props");
        var props = $"""
            <Project>
              <PropertyGroup>
                <BaseIntermediateOutputPath>{SecurityElement.Escape(relativeIntermediate)}</BaseIntermediateOutputPath>
                <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
              </PropertyGroup>
            </Project>
            """;
        await WriteUtf8Async(propsPath, props + "\n", token);
        var projectPath = Path.Combine(directory, $"{typeName}.csproj");
        var project = $"""
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <TargetFramework>net10.0</TargetFramework>
                <PlatformTarget>x64</PlatformTarget>
                <ImplicitUsings>enable</ImplicitUsings>
                <Nullable>enable</Nullable>
                <AssemblyName>{SecurityElement.Escape(Path.GetFileNameWithoutExtension(entry))}</AssemblyName>
                <RootNamespace>{SecurityElement.Escape(namespaceName)}</RootNamespace>
                <Version>{SecurityElement.Escape(request.Version)}</Version>
                <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
                <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
                <OutputPath>./</OutputPath>
                <DebugType>none</DebugType>
                <GenerateDependencyFile>false</GenerateDependencyFile>
              </PropertyGroup>
              <ItemGroup>
                <ProjectReference Include="{SecurityElement.Escape(relativeSdk)}" Private="false" />
              </ItemGroup>
            </Project>
            """;
        await WriteUtf8Async(projectPath, project + "\n", token);
        var sourcePath = Path.Combine(directory, "Mod.cs");
        var source = $$"""
            using Dluma.Managed.Sdk;

            namespace {{namespaceName}};

            public sealed class {{className}} : IDlumaMod
            {
                private IModContext? _context;

                public void OnStart(IModContext context)
                {
                    _context = context;
                    context.Log.Info("DLUMA API 1.0 C# mod started");
                }

                public void OnStop(DlumaStopReason reason)
                {
                    _context?.Log.Info($"C# mod stopped: {reason}");
                    _context = null;
                }
            }
            """;
        await WriteUtf8Async(sourcePath, source + "\n", token);
        return sourcePath;
    }

    private static async Task<string> CreateNativeAsync(
        string directory,
        ModTemplateRequest request,
        string entry,
        CancellationToken token)
    {
        var sdkHeader = Path.Combine(
            request.RepositoryRoot, "sdk", "c", "dluma_api.h");
        var relativeHeader = Path.GetRelativePath(directory, sdkHeader).Replace('\\', '/');
        var intermediateDirectory = Path.Combine(
            request.RepositoryRoot, "build", "workbench-mods", request.Id, "native");
        var relativeIntermediate = Path.GetRelativePath(directory, intermediateDirectory);
        var sourcePath = Path.Combine(directory, "mod.cpp");
        var source = $$"""
            #include <windows.h>
            #include "{{relativeHeader}}"

            static DlumaStatus DLUMA_API_CALL pre_game(const DlumaApiV1* api) { return api ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT; }
            static DlumaStatus DLUMA_API_CALL post_game(const DlumaApiV1*) { return DLUMA_STATUS_OK; }
            static DlumaStatus DLUMA_API_CALL scope_activated(const DlumaApiV1*, const DlumaScopeActivationV1*) { return DLUMA_STATUS_OK; }
            static void DLUMA_API_CALL scope_deactivated(const DlumaApiV1*, const DlumaScopeActivationV1*) {}
            static void DLUMA_API_CALL shutdown_mod(const DlumaApiV1*, uint32_t) {}
            static DlumaStatus DLUMA_API_CALL event_mod(const DlumaApiV1*, const DlumaModEventV1*) { return DLUMA_STATUS_OK; }

            static const DlumaNativeModApiV2 mod_api = {
                sizeof(DlumaNativeModApiV2), DLUMA_NATIVE_MOD_ABI_V2,
                pre_game, post_game, scope_activated, scope_deactivated,
                shutdown_mod, event_mod
            };

            BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

            extern "C" DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL
            DLUMANativeMod_GetApiV2(void) { return &mod_api; }
            """;
        await WriteUtf8Async(sourcePath, source + "\n", token);
        var buildPath = Path.Combine(directory, "build.cmd");
        var setupMsvc = Path.GetRelativePath(
            directory, Path.Combine(request.RepositoryRoot, "tools", "setup_msvc.bat"));
        var build = $"""
            @echo off
            setlocal EnableExtensions
            cd /d "%~dp0"
            where cl.exe >nul 2>nul || call "{setupMsvc}" >nul || exit /b 1
            set "DLUMA_OUT={relativeIntermediate}"
            if not exist "%DLUMA_OUT%" mkdir "%DLUMA_OUT%" || exit /b 1
            cl.exe /nologo /c /TP /std:c++20 /MT /O2 /Oi /GS /guard:cf /Zl /W4 /WX /EHs-c- /GR- /Fo"%DLUMA_OUT%\mod.obj" mod.cpp || exit /b 1
            link.exe /nologo /dll /machine:x64 /entry:DllMain /nodefaultlib /dynamicbase /nxcompat /highentropyva /guard:cf /opt:ref /opt:icf /implib:"%DLUMA_OUT%\mod.lib" /pdb:"%DLUMA_OUT%\mod.pdb" /out:"{entry}" "%DLUMA_OUT%\mod.obj" kernel32.lib BufferOverflowU.lib libcmt.lib vcruntime.lib || exit /b 1
            echo Built: %CD%\{entry}
            """;
        build = build.Replace("\n", "\r\n", StringComparison.Ordinal) + "\r\n";
        await WriteUtf8Async(buildPath, build, token);
        return sourcePath;
    }

    private static string ConfigurationTemplate() => """
        {
          "schemaVersion": 1,
          "categories": [
            {
              "id": "general",
              "name": "General",
              "settings": [
                {
                  "key": "enabled",
                  "name": "Enabled",
                  "description": "Enable this mod feature.",
                  "type": "boolean",
                  "default": true
                }
              ]
            }
          ]
        }
        """ + "\n";

    private static JsonSerializerOptions JsonOptions() => new() { WriteIndented = true };
    private static string LanguageText(ModLanguage language) => language switch
    {
        ModLanguage.Lua => "lua",
        ModLanguage.CSharp => "csharp",
        _ => "native"
    };
    private static string ScopeText(ModScope scope) => scope.ToString().ToLowerInvariant();
    private static string SafeTypeName(string name)
    {
        var value = Regex.Replace(name, "[^A-Za-z0-9_]", string.Empty);
        if (value.Length == 0 || char.IsDigit(value[0]))
            value = "DlumaMod" + value;
        return value;
    }
    private static Task WriteUtf8Async(string path, string text, CancellationToken token) =>
        File.WriteAllTextAsync(path, text, new UTF8Encoding(false), token);
}

public sealed record CommandResult(int ExitCode, string Output, string Error)
{
    public bool Succeeded => ExitCode == 0;
    public string Combined => string.Join(
        Environment.NewLine,
        new[] { Output, Error }.Where(value => !string.IsNullOrWhiteSpace(value)));
}

public static class ProcessService
{
    public static async Task<CommandResult> RunAsync(
        string executable,
        IEnumerable<string> arguments,
        string workingDirectory,
        CancellationToken cancellationToken = default)
    {
        var start = new ProcessStartInfo(executable)
        {
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (var argument in arguments)
            start.ArgumentList.Add(argument);
        using var process = new Process { StartInfo = start };
        if (!process.Start())
            return new CommandResult(-1, string.Empty, $"Could not start {executable}.");
        var output = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var error = process.StandardError.ReadToEndAsync(cancellationToken);
        await process.WaitForExitAsync(cancellationToken);
        return new CommandResult(process.ExitCode, await output, await error);
    }

    public static void Open(string executable, IEnumerable<string> arguments, string workingDirectory)
    {
        var start = new ProcessStartInfo(executable)
        {
            WorkingDirectory = workingDirectory,
            UseShellExecute = true
        };
        foreach (var argument in arguments)
            start.ArgumentList.Add(argument);
        Process.Start(start);
    }
}

public sealed record PrerequisiteStatus(bool DotNet10, bool MsvcX64, string Summary);

public static class WorkbenchCommandService
{
    public static async Task<PrerequisiteStatus> DetectPrerequisitesAsync(
        CancellationToken cancellationToken = default)
    {
        var dotnet = await ProcessService.RunAsync(
            "dotnet", new[] { "--list-sdks" }, Environment.CurrentDirectory, cancellationToken);
        var hasDotnet = dotnet.Succeeded && dotnet.Output.Split('\n')
            .Any(line => line.TrimStart().StartsWith("10.", StringComparison.Ordinal));
        var compiler = await ProcessService.RunAsync(
            "where.exe", new[] { "cl.exe" }, Environment.CurrentDirectory, cancellationToken);
        var hasMsvc = compiler.Succeeded;
        if (!hasMsvc)
        {
            var programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            var vswhere = Path.Combine(
                programFilesX86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
            if (File.Exists(vswhere))
            {
                var discovered = await ProcessService.RunAsync(
                    vswhere,
                    new[]
                    {
                        "-latest", "-products", "*", "-requires",
                        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                        "-property", "installationPath"
                    },
                    Environment.CurrentDirectory,
                    cancellationToken);
                hasMsvc = discovered.Succeeded && !string.IsNullOrWhiteSpace(discovered.Output);
            }
        }
        var summary = $".NET 10 x64 SDK: {(hasDotnet ? "ready" : "missing")} | MSVC x64 Build Tools: {(hasMsvc ? "ready" : "missing")}";
        return new PrerequisiteStatus(hasDotnet, hasMsvc, summary);
    }

    public static Task<CommandResult> ValidateAsync(string repositoryRoot, string modDirectory, bool requireLock, CancellationToken token = default)
    {
        var arguments = new List<string>
        {
            Path.Combine(repositoryRoot, "tools", "validate_mod_package.py"),
            "validate",
            modDirectory
        };
        if (requireLock) arguments.Add("--require-lock");
        return ProcessService.RunAsync("python", arguments, repositoryRoot, token);
    }

    public static Task<CommandResult> LockAsync(string repositoryRoot, string modDirectory, CancellationToken token = default) =>
        ProcessService.RunAsync(
            "python",
            new[] { Path.Combine(repositoryRoot, "tools", "validate_mod_package.py"), "lock", modDirectory },
            repositoryRoot,
            token);

    public static Task<CommandResult> PackageAsync(string repositoryRoot, string modDirectory, string output, CancellationToken token = default) =>
        ProcessService.RunAsync(
            "python",
            new[] { Path.Combine(repositoryRoot, "tools", "validate_mod_package.py"), "package", modDirectory, "--out", output },
            repositoryRoot,
            token);

    public static async Task<CommandResult> BuildAsync(ModLanguage language, string modDirectory, CancellationToken token = default)
    {
        if (language == ModLanguage.Lua)
            return new CommandResult(0, "Lua requires no external compiler. Run Validate and Package.", string.Empty);
        if (language == ModLanguage.CSharp)
            return await ProcessService.RunAsync("dotnet", new[] { "build", "-c", "Release" }, modDirectory, token);
        return await ProcessService.RunAsync("cmd.exe", new[] { "/d", "/s", "/c", "build.cmd" }, modDirectory, token);
    }

    public static async Task StageAsync(string package, string destination, CancellationToken token = default)
    {
        Directory.CreateDirectory(destination);
        var target = Path.Combine(destination, Path.GetFileName(package));
        await using var source = File.Open(package, FileMode.Open, FileAccess.Read, FileShare.Read);
        await using var output = File.Open(target, FileMode.Create, FileAccess.Write, FileShare.None);
        await source.CopyToAsync(output, token);
    }
}

public sealed record LuaDiagnostic(int Line, string Severity, string Message);

public static class LuaDiagnosticService
{
    public static IReadOnlyList<LuaDiagnostic> Analyze(string text)
    {
        var diagnostics = new List<LuaDiagnostic>();
        var lines = text.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n');
        var balance = 0;
        for (var index = 0; index < lines.Length; ++index)
        {
            var line = lines[index].Trim();
            if (line.Contains("dlua", StringComparison.Ordinal))
                diagnostics.Add(new LuaDiagnostic(index + 1, "Error", "Legacy dlua namespace is unsupported; use dluma."));
            if (line.Contains("loadfile", StringComparison.Ordinal) || line.Contains("require(", StringComparison.Ordinal))
                diagnostics.Add(new LuaDiagnostic(index + 1, "Warning", "Only approval-bound package-local modules may be loaded."));
            if (Regex.IsMatch(line, "^(function|if .* then$|for .* do$|while .* do$|repeat$)"))
                balance++;
            if (line == "end" || line.StartsWith("end ", StringComparison.Ordinal) || line == "until")
                balance--;
            if (balance < 0)
            {
                diagnostics.Add(new LuaDiagnostic(index + 1, "Error", "Unexpected block terminator."));
                balance = 0;
            }
        }
        if (balance > 0)
            diagnostics.Add(new LuaDiagnostic(lines.Length, "Error", $"{balance} block(s) are not closed."));
        return diagnostics;
    }
}
