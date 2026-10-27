using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace Dluma.QuestGraph.Core;

public sealed class QuestCatalog
{
    private readonly IReadOnlyDictionary<string, QuestCatalogEntry> _macros;
    private readonly IReadOnlyDictionary<string, QuestCatalogEntry> _entriesByName;

    internal QuestCatalog(
        IReadOnlyList<QuestCatalogEntry> entries,
        IReadOnlyList<QuestCatalogProvenance> provenance,
        string catalogSha256)
    {
        Entries = entries;
        Provenance = provenance;
        CatalogSha256 = catalogSha256;
        _macros = entries
            .Where(entry => entry.Kind is QuestCatalogEntryKind.Macro or QuestCatalogEntryKind.Phase)
            .GroupBy(entry => NormalizeName(entry.Name), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(
                group => group.Key,
                group => group.First(),
                StringComparer.OrdinalIgnoreCase);
        _entriesByName = entries
            .GroupBy(entry => NormalizeName(entry.Name), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.OrdinalIgnoreCase);
    }

    public IReadOnlyList<QuestCatalogEntry> Entries { get; }
    public IReadOnlyList<QuestCatalogProvenance> Provenance { get; }
    public string CatalogSha256 { get; }
    public IReadOnlyList<QuestCatalogEntry> InsertableEntries => Entries.Where(entry => entry.IsInsertable).ToArray();

    public bool ContainsMacro(string name) => _macros.ContainsKey(NormalizeName(name));

    public bool TryGetMacro(string name, out QuestCatalogEntry? entry) =>
        _macros.TryGetValue(NormalizeName(name), out entry);

    public bool TryGetEntry(string name, out QuestCatalogEntry? entry) =>
        _entriesByName.TryGetValue(NormalizeName(name), out entry);

    private static string NormalizeName(string name) => name.Trim().TrimStart(',');
}

public static partial class QuestCatalogExtractor
{
    private const int MaximumInputBytes = 32 * 1024 * 1024;

    private static QuestCatalog ExtractLegacy(IEnumerable<QuestCatalogInput> inputs)
    {
        ArgumentNullException.ThrowIfNull(inputs);
        var entries = new Dictionary<string, EntryBuilder>(StringComparer.OrdinalIgnoreCase);
        var provenance = new List<QuestCatalogProvenance>();

        foreach (var input in inputs)
        {
            ArgumentNullException.ThrowIfNull(input);
            ArgumentException.ThrowIfNullOrWhiteSpace(input.Name);
            ArgumentNullException.ThrowIfNull(input.Content);
            var bytes = ReadBounded(input.Content, MaximumInputBytes);
            var format = ResolveFormat(input.Name, input.Format, bytes);
            var sourceName = Path.GetFileName(input.Name);
            if (string.IsNullOrWhiteSpace(sourceName))
                sourceName = "quest-catalog";
            provenance.Add(new QuestCatalogProvenance(
                sourceName,
                format,
                QuestHash.Sha256(bytes)));
            Parse(format, sourceName, bytes, entries);
        }

        var materialized = entries.Values
            .Select(builder => builder.Build())
            .OrderBy(entry => entry.Kind)
            .ThenBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        var orderedProvenance = provenance
            .OrderBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .ThenBy(item => item.Format)
            .ToArray();
        return new QuestCatalog(
            materialized,
            orderedProvenance,
            CatalogHash(materialized, orderedProvenance));
    }

    public static QuestCatalog ExtractFiles(IEnumerable<string> paths)
    {
        ArgumentNullException.ThrowIfNull(paths);
        var inputs = new List<QuestCatalogInput>();
        var streams = new List<MemoryStream>();
        try
        {
            foreach (var path in paths)
            {
                ArgumentException.ThrowIfNullOrWhiteSpace(path);
                var bytes = File.ReadAllBytes(path);
                if (bytes.Length > MaximumInputBytes)
                    throw new InvalidDataException($"Quest catalog input exceeds {MaximumInputBytes} bytes.");
                var stream = new MemoryStream(bytes, writable: false);
                streams.Add(stream);
                inputs.Add(new QuestCatalogInput(Path.GetFileName(path), QuestCatalogFormat.Auto, stream));
            }
            return Extract(inputs);
        }
        finally
        {
            foreach (var stream in streams)
                stream.Dispose();
        }
    }

    private static void Parse(
        QuestCatalogFormat format,
        string source,
        byte[] bytes,
        Dictionary<string, EntryBuilder> entries)
    {
        switch (format)
        {
            case QuestCatalogFormat.Edf:
                ParseEdf(DecodeText(bytes), source, entries);
                break;
            case QuestCatalogFormat.QuestEditorConfig:
                ParseConfig(DecodeText(bytes), source, entries);
                break;
            case QuestCatalogFormat.PhaseDefinitionsXml:
                ParsePhaseDefinitions(bytes, source, entries);
                break;
            case QuestCatalogFormat.QuestObjectDefinitionsXml:
                ParseObjectDefinitions(bytes, source, entries);
                break;
            default:
                throw new InvalidDataException($"Unsupported quest catalog format: {format}.");
        }
    }

    private static void ParseEdf(
        string text,
        string source,
        Dictionary<string, EntryBuilder> entries)
    {
        foreach (var rawLine in SplitLines(text))
        {
            var match = EdfDefinitionPattern().Match(rawLine);
            if (!match.Success)
                continue;
            var name = match.Groups["name"].Value;
            var builder = Get(entries, QuestCatalogEntryKind.Macro, name);
            builder.Sources.Add(source);
            var arguments = match.Groups["arguments"].Value
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            for (var index = 0; index < arguments.Length; ++index)
            {
                var argument = arguments[index].Trim('[', ']', '<', '>', '(', ')', ',');
                if (argument.Length == 0 || argument == "_")
                    continue;
                builder.AddParameter(new QuestCatalogParameter(
                    argument,
                    "string",
                    true,
                    index,
                    Array.Empty<string>()));
            }
        }
    }

    private static void ParseConfig(
        string text,
        string source,
        Dictionary<string, EntryBuilder> entries)
    {
        EntryBuilder? current = null;
        foreach (var line in SplitLines(text))
        {
            var block = ConfigBlockPattern().Match(line);
            if (block.Success)
            {
                current = Get(entries, QuestCatalogEntryKind.Macro, block.Groups["name"].Value);
                current.Sources.Add(source);
                continue;
            }
            var expand = ConfigExpandPattern().Match(line);
            if (current is not null && expand.Success)
            {
                var child = expand.Groups["name"].Value;
                current.AddChild(new QuestCatalogChild(child, "flow", "*"));
                Get(entries, QuestCatalogEntryKind.Branch, child).Sources.Add(source);
            }
        }
    }

    private static void ParsePhaseDefinitions(
        byte[] bytes,
        string source,
        Dictionary<string, EntryBuilder> entries)
    {
        var document = LoadXml(bytes);
        foreach (var phase in document.Descendants()
            .Where(element => element.Name.LocalName.Equals("PhaseType", StringComparison.OrdinalIgnoreCase)))
        {
            var name = phase.Attribute("name")?.Value;
            if (string.IsNullOrWhiteSpace(name))
                continue;
            var builder = Get(entries, QuestCatalogEntryKind.Phase, name);
            builder.Sources.Add(source);
            foreach (var child in phase.Elements())
            {
                if (child.Name.LocalName.Equals("attribute", StringComparison.OrdinalIgnoreCase))
                {
                    var parameterName = child.Attribute("name")?.Value;
                    if (string.IsNullOrWhiteSpace(parameterName))
                        continue;
                    var values = child.Elements()
                        .Where(value => value.Name.LocalName.Equals("value", StringComparison.OrdinalIgnoreCase))
                        .Select(value => value.Value.Trim())
                        .Where(value => value.Length > 0)
                        .Distinct(StringComparer.Ordinal)
                        .ToArray();
                    builder.AddParameter(new QuestCatalogParameter(
                        parameterName,
                        child.Attribute("type")?.Value ?? "string",
                        !string.Equals(child.Attribute("optional")?.Value, "true", StringComparison.OrdinalIgnoreCase),
                        null,
                        values));
                    continue;
                }

                var childName = child.Attribute("name")?.Value ?? child.Name.LocalName;
                builder.AddChild(new QuestCatalogChild(
                    childName,
                    child.Name.LocalName,
                    child.Attribute("quantity")?.Value ?? "?"));
            }
        }
    }

    private static void ParseObjectDefinitions(
        byte[] bytes,
        string source,
        Dictionary<string, EntryBuilder> entries)
    {
        var document = LoadXml(bytes);
        foreach (var definition in document.Descendants()
            .Where(element => element.Name.LocalName.Equals("QuestObject", StringComparison.OrdinalIgnoreCase)))
        {
            var name = definition.Attribute("name")?.Value;
            if (string.IsNullOrWhiteSpace(name))
                continue;
            var builder = Get(entries, QuestCatalogEntryKind.QuestObject, name);
            builder.Sources.Add(source);
            foreach (var child in definition.Elements())
            {
                var childName = child.Attribute("name")?.Value;
                if (!string.IsNullOrWhiteSpace(childName))
                {
                    builder.AddChild(new QuestCatalogChild(
                        childName,
                        child.Name.LocalName,
                        child.Attribute("quantity")?.Value ?? "*"));
                }
            }
        }
    }

    private static XDocument LoadXml(byte[] bytes)
    {
        using var stream = new MemoryStream(bytes, writable: false);
        using var reader = XmlReader.Create(stream, new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null,
            IgnoreComments = true,
            MaxCharactersInDocument = MaximumInputBytes
        });
        return XDocument.Load(reader, LoadOptions.None);
    }

    private static QuestCatalogFormat ResolveFormat(
        string name,
        QuestCatalogFormat requested,
        byte[] bytes)
    {
        if (requested != QuestCatalogFormat.Auto)
            return requested;
        var extension = Path.GetExtension(name);
        if (extension.Equals(".edf", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.Edf;
        if (Path.GetFileName(name).Contains("QuestEditorConfig", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.QuestEditorConfig;
        if (extension.Equals(".xml", StringComparison.OrdinalIgnoreCase))
        {
            var prefix = DecodeText(bytes.AsSpan(0, Math.Min(bytes.Length, 1024)).ToArray());
            return prefix.Contains("QuestObjectDefinitions", StringComparison.OrdinalIgnoreCase)
                ? QuestCatalogFormat.QuestObjectDefinitionsXml
                : QuestCatalogFormat.PhaseDefinitionsXml;
        }
        throw new InvalidDataException($"Could not infer quest catalog format for '{Path.GetFileName(name)}'.");
    }

    private static EntryBuilder Get(
        Dictionary<string, EntryBuilder> entries,
        QuestCatalogEntryKind kind,
        string rawName)
    {
        var name = rawName.Trim().TrimStart(',');
        var key = $"{kind}:{name}";
        if (!entries.TryGetValue(key, out var builder))
        {
            builder = new EntryBuilder(name, kind);
            entries.Add(key, builder);
        }
        return builder;
    }

    private static byte[] ReadBounded(Stream stream, int maximum)
    {
        using var memory = new MemoryStream();
        var buffer = new byte[81920];
        while (true)
        {
            var read = stream.Read(buffer, 0, buffer.Length);
            if (read == 0)
                break;
            if (memory.Length + read > maximum)
                throw new InvalidDataException($"Quest catalog input exceeds {maximum} bytes.");
            memory.Write(buffer, 0, read);
        }
        return memory.ToArray();
    }

    private static string DecodeText(byte[] bytes)
    {
        if (bytes.AsSpan().StartsWith(Encoding.UTF8.GetPreamble()))
            return new UTF8Encoding(true, true).GetString(bytes)[1..];
        return new UTF8Encoding(false, true).GetString(bytes);
    }

    private static IEnumerable<string> SplitLines(string text) =>
        text.Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Split('\n');

    private static string CatalogHash(
        IReadOnlyList<QuestCatalogEntry> entries,
        IReadOnlyList<QuestCatalogProvenance> provenance)
    {
        var canonical = new StringBuilder();
        foreach (var item in provenance)
            canonical.Append(item.Name).Append('|').Append(item.Format).Append('|').Append(item.Sha256).Append('\n');
        foreach (var entry in entries)
        {
            canonical.Append(entry.Kind).Append('|').Append(entry.Name).Append('\n');
            foreach (var parameter in entry.Parameters)
            {
                canonical.Append("P|").Append(parameter.Position).Append('|')
                    .Append(parameter.Name).Append('|').Append(parameter.Type).Append('|')
                    .Append(parameter.IsRequired).Append('|')
                    .AppendJoin(',', parameter.Values).Append('\n');
            }
            foreach (var child in entry.AllowedChildren)
                canonical.Append("C|").Append(child.Name).Append('|').Append(child.Kind).Append('|').Append(child.Quantity).Append('\n');
        }
        return QuestHash.Sha256(canonical.ToString());
    }

    [GeneratedRegex("^\\s*,(?<name>[A-Za-z0-9_.-]+)(?<arguments>.*?)=\\s*$", RegexOptions.CultureInvariant)]
    private static partial Regex EdfDefinitionPattern();

    [GeneratedRegex("Block\\(\\s*\"(?<name>[^\"]+)\"\\s*\\)", RegexOptions.CultureInvariant)]
    private static partial Regex ConfigBlockPattern();

    [GeneratedRegex("Expand\\(\\s*\"(?<name>[^\"]+)\"\\s*\\)", RegexOptions.CultureInvariant)]
    private static partial Regex ConfigExpandPattern();

    private sealed class EntryBuilder
    {
        private readonly Dictionary<string, QuestCatalogParameter> _parameters =
            new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, QuestCatalogChild> _children =
            new(StringComparer.OrdinalIgnoreCase);

        public EntryBuilder(string name, QuestCatalogEntryKind kind)
        {
            Name = name;
            Kind = kind;
        }

        public string Name { get; }
        public QuestCatalogEntryKind Kind { get; }
        public HashSet<string> Sources { get; } = new(StringComparer.OrdinalIgnoreCase);

        public void AddParameter(QuestCatalogParameter parameter)
        {
            var key = $"{parameter.Position}:{parameter.Name}";
            _parameters.TryAdd(key, parameter);
        }

        public void AddChild(QuestCatalogChild child)
        {
            var key = $"{child.Kind}:{child.Name}";
            _children.TryAdd(key, child);
        }

        public QuestCatalogEntry Build() => new(
            Name,
            Kind,
            _parameters.Values
                .OrderBy(parameter => parameter.Position ?? int.MaxValue)
                .ThenBy(parameter => parameter.Name, StringComparer.OrdinalIgnoreCase)
                .ToArray(),
            _children.Values
                .OrderBy(child => child.Kind, StringComparer.OrdinalIgnoreCase)
                .ThenBy(child => child.Name, StringComparer.OrdinalIgnoreCase)
                .ToArray(),
            Sources.OrderBy(source => source, StringComparer.OrdinalIgnoreCase).ToArray());
    }
}
