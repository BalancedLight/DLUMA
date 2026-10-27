using System.Reflection;
using System.Runtime.Loader;

namespace Dluma.Managed.Bootstrap;

internal sealed class ModAssemblyLoadContext : AssemblyLoadContext
{
    private readonly Dictionary<string, byte[]> _assemblies;

    internal ModAssemblyLoadContext(
        string modId,
        IReadOnlyDictionary<string, byte[]> artifacts)
        : base($"DLUMA:{modId}", isCollectible: false)
    {
        _assemblies = new Dictionary<string, byte[]>(
            StringComparer.OrdinalIgnoreCase);
        foreach (var (relativePath, bytes) in artifacts)
        {
            if (!relativePath.EndsWith(
                    ".dll",
                    StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var simpleName = Path.GetFileNameWithoutExtension(relativePath);
            if (string.IsNullOrWhiteSpace(simpleName) ||
                !_assemblies.TryAdd(simpleName, bytes))
            {
                throw new InvalidDataException(
                    $"Managed artifact name is ambiguous: {relativePath}");
            }
        }
    }

    internal Assembly LoadEntryAssembly(byte[] bytes)
    {
        ArgumentNullException.ThrowIfNull(bytes);
        using var stream = new MemoryStream(bytes, writable: false);
        return LoadFromStream(stream);
    }

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        if (string.Equals(
                assemblyName.Name,
                typeof(Dluma.Managed.Sdk.IDlumaMod).Assembly.GetName().Name,
                StringComparison.Ordinal))
        {
            return typeof(Dluma.Managed.Sdk.IDlumaMod).Assembly;
        }

        if (assemblyName.Name is not null &&
            _assemblies.TryGetValue(assemblyName.Name, out var bytes))
        {
            using var stream = new MemoryStream(bytes, writable: false);
            return LoadFromStream(stream);
        }

        /*
         * Returning null allows only the runtime/default context to satisfy
         * framework assemblies. The mod has no pathname-based probing root,
         * so an undeclared private dependency cannot be picked up beside the
         * approved package.
         */
        return null;
    }

    protected override nint LoadUnmanagedDll(string unmanagedDllName)
    {
        /*
         * The normal DLUMA managed SDK never resolves native dependencies for a
         * mod. C# remains full trust and can explicitly bypass the SDK, which
         * is why the loader presents the full-trust warning and approval.
         */
        return 0;
    }
}
