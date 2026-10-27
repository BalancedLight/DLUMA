using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.Services;

public static partial class QuestDiskContextProvider
{
    private const int MaximumDirectories = 2_048;
    private const int MaximumFiles = 20_000;
    private const int MaximumCandidateBytes = 4 * 1024 * 1024;
    private const long MaximumTotalBytes = 64L * 1024 * 1024;

    public static async Task<QuestContextSnapshot> LoadAsync(
        string projectRoot,
        long generation,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        var root = Path.GetFullPath(projectRoot);
        if (!Directory.Exists(root))
            throw new DirectoryNotFoundException("The active quest project root does not exist.");
        var rootInfo = new DirectoryInfo(root);
        if ((rootInfo.Attributes & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("The active project root cannot be a reparse point.");

        var objects = new Dictionary<string, ObjectBuilder>(StringComparer.OrdinalIgnoreCase);
        var texts = new Dictionary<string, QuestContextText>(StringComparer.OrdinalIgnoreCase);
        var speakers = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var dialogueGroups = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var questReferences = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var queue = new Queue<DirectoryInfo>();
        queue.Enqueue(rootInfo);
        var directories = 0;
        var files = 0;
        long totalBytes = 0;

        while (queue.Count > 0 && directories < MaximumDirectories && files < MaximumFiles)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var directory = queue.Dequeue();
            directories++;
            IEnumerable<FileSystemInfo> entries;
            try
            {
                entries = directory.EnumerateFileSystemInfos()
                    .OrderBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase)
                    .ToArray();
            }
            catch (Exception exception) when (
                exception is UnauthorizedAccessException or IOException)
            {
                continue;
            }

            foreach (var entry in entries)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if ((entry.Attributes & FileAttributes.ReparsePoint) != 0)
                    continue;
                if (entry is DirectoryInfo childDirectory)
                {
                    if (!ShouldSkipDirectory(childDirectory.Name))
                        queue.Enqueue(childDirectory);
                    continue;
                }
                if (entry is not FileInfo file || ++files > MaximumFiles)
                    break;
                if (file.Extension.Equals(".pxsl", StringComparison.OrdinalIgnoreCase))
                    questReferences.Add(Path.GetFileNameWithoutExtension(file.Name));
                if (!IsCandidate(file) || file.Length <= 0 || file.Length > MaximumCandidateBytes ||
                    totalBytes + file.Length > MaximumTotalBytes)
                    continue;
                try
                {
                    var bytes = await ReadCandidateBoundedAsync(
                            file,
                            MaximumTotalBytes - totalBytes,
                            cancellationToken)
                        .ConfigureAwait(false);
                    totalBytes += bytes.Length;
                    if (file.Extension.Equals(".xml", StringComparison.OrdinalIgnoreCase))
                        ReadXml(bytes, objects, texts, speakers, dialogueGroups);
                    else
                        ReadText(bytes, objects, texts, speakers, dialogueGroups);
                }
                catch (Exception exception) when (
                    exception is IOException or UnauthorizedAccessException or
                    XmlException or DecoderFallbackException or InvalidDataException)
                {
                    // A malformed optional resource cannot block offline PXSL editing.
                }
            }
        }

        return QuestContextBudget.Apply(new QuestContextSnapshot(
            generation,
            rootInfo.Name,
            string.Empty,
            objects.Values
                .Select(value => value.Build())
                .OrderBy(value => value.Name, StringComparer.OrdinalIgnoreCase)
                .Take(QuestBridgeProtocol.MaximumCollectionItems)
                .ToArray(),
            texts.Values
                .OrderBy(value => value.Id, StringComparer.OrdinalIgnoreCase)
                .Take(QuestBridgeProtocol.MaximumCollectionItems)
                .ToArray(),
            speakers.OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray(),
            dialogueGroups.OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray(),
            questReferences.OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray()),
            QuestBridgeProtocol.MaximumCollectionItems);
    }

    public static QuestContextSnapshot Merge(
        QuestContextSnapshot disk,
        QuestContextSnapshot live)
    {
        var objects = disk.Objects.ToDictionary(value => value.Name, StringComparer.OrdinalIgnoreCase);
        foreach (var value in live.Objects)
            objects[value.Name] = value;
        var texts = disk.Texts.ToDictionary(value => value.Id, StringComparer.OrdinalIgnoreCase);
        foreach (var value in live.Texts)
            texts[value.Id] = value;
        return QuestContextBudget.Apply(live with
        {
            Objects = objects.Values.OrderBy(value => value.Name, StringComparer.OrdinalIgnoreCase).ToArray(),
            Texts = texts.Values.OrderBy(value => value.Id, StringComparer.OrdinalIgnoreCase).ToArray(),
            Speakers = disk.Speakers.Concat(live.Speakers).Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray(),
            DialogueGroups = disk.DialogueGroups.Concat(live.DialogueGroups).Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray(),
            QuestReferences = disk.QuestReferences.Concat(live.QuestReferences).Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray()
        }, QuestBridgeProtocol.MaximumCollectionItems);
    }

    private static void ReadXml(
        byte[] bytes,
        Dictionary<string, ObjectBuilder> objects,
        Dictionary<string, QuestContextText> texts,
        HashSet<string> speakers,
        HashSet<string> dialogueGroups)
    {
        using var stream = new MemoryStream(bytes, writable: false);
        using var reader = XmlReader.Create(stream, new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null,
            IgnoreComments = true,
            MaxCharactersInDocument = MaximumCandidateBytes
        });
        while (reader.Read())
        {
            if (reader.NodeType != XmlNodeType.Element)
                continue;
            var name = Attribute(reader, "name") ?? Attribute(reader, "object");
            var className = Attribute(reader, "class") ?? Attribute(reader, "type");
            var role = Attribute(reader, "role");
            if (!string.IsNullOrWhiteSpace(name) && !string.IsNullOrWhiteSpace(className))
                AddObject(objects, name, className, role);

            var id = Attribute(reader, "id") ?? Attribute(reader, "key");
            var value = Attribute(reader, "value") ?? Attribute(reader, "text");
            if (!string.IsNullOrWhiteSpace(id) && value is not null)
                texts[id] = new QuestContextText(id, value);
            AddIfPresent(speakers, Attribute(reader, "speaker"));
            AddIfPresent(dialogueGroups, Attribute(reader, "dialogueGroup") ?? Attribute(reader, "dialog_group"));
        }
    }

    private static void ReadText(
        byte[] bytes,
        Dictionary<string, ObjectBuilder> objects,
        Dictionary<string, QuestContextText> texts,
        HashSet<string> speakers,
        HashSet<string> dialogueGroups)
    {
        var text = new UTF8Encoding(false, true).GetString(bytes);
        foreach (Match match in TextEntryPattern().Matches(text))
            texts[match.Groups["id"].Value] = new QuestContextText(
                match.Groups["id"].Value,
                match.Groups["value"].Value);
        foreach (Match match in ObjectEntryPattern().Matches(text))
            AddObject(
                objects,
                match.Groups["name"].Value,
                match.Groups["class"].Value,
                match.Groups["role"].Success ? match.Groups["role"].Value : null);
        foreach (Match match in SpeakerPattern().Matches(text))
            AddIfPresent(speakers, match.Groups["value"].Value);
        foreach (Match match in DialogueGroupPattern().Matches(text))
            AddIfPresent(dialogueGroups, match.Groups["value"].Value);
    }

    private static string? Attribute(XmlReader reader, string name) =>
        reader.GetAttribute(name) ?? reader.GetAttribute(name.ToUpperInvariant());

    private static void AddObject(
        Dictionary<string, ObjectBuilder> objects,
        string name,
        string className,
        string? role)
    {
        if (!objects.TryGetValue(name, out var value))
        {
            value = new ObjectBuilder(name, className);
            objects.Add(name, value);
        }
        AddIfPresent(value.Roles, role);
    }

    private static void AddIfPresent(HashSet<string> values, string? value)
    {
        if (!string.IsNullOrWhiteSpace(value) && value.Length <= 512)
            values.Add(value.Trim());
    }

    private static bool IsCandidate(FileInfo file) =>
        file.Extension.Equals(".xml", StringComparison.OrdinalIgnoreCase) ||
        file.Extension.Equals(".scr", StringComparison.OrdinalIgnoreCase) ||
        file.Extension.Equals(".edf", StringComparison.OrdinalIgnoreCase) ||
        file.Extension.Equals(".txt", StringComparison.OrdinalIgnoreCase) ||
        file.Extension.Equals(".csv", StringComparison.OrdinalIgnoreCase);

    private static bool ShouldSkipDirectory(string name) =>
        name.Equals(".git", StringComparison.OrdinalIgnoreCase) ||
        name.Equals("bin", StringComparison.OrdinalIgnoreCase) ||
        name.Equals("obj", StringComparison.OrdinalIgnoreCase) ||
        name.Equals("node_modules", StringComparison.OrdinalIgnoreCase);

    [GeneratedRegex("(?:Text|String)\\s*\\(\\s*\"(?<id>[^\"]{1,512})\"\\s*,\\s*\"(?<value>[^\"]{0,4096})\"", RegexOptions.CultureInvariant)]
    private static partial Regex TextEntryPattern();

    [GeneratedRegex("(?:Object|QuestObject)\\s*\\(\\s*\"(?<name>[^\"]{1,512})\"\\s*,\\s*\"(?<class>[^\"]{1,512})\"(?:\\s*,\\s*\"(?<role>[^\"]{1,512})\")?", RegexOptions.CultureInvariant)]
    private static partial Regex ObjectEntryPattern();

    [GeneratedRegex("Speaker\\s*\\(\\s*\"(?<value>[^\"]{1,512})\"", RegexOptions.CultureInvariant)]
    private static partial Regex SpeakerPattern();

    [GeneratedRegex("(?:DialogueGroup|DialogGroup)\\s*\\(\\s*\"(?<value>[^\"]{1,512})\"", RegexOptions.CultureInvariant)]
    private static partial Regex DialogueGroupPattern();

    private sealed class ObjectBuilder
    {
        public ObjectBuilder(string name, string className)
        {
            Name = name;
            ClassName = className;
        }

        public string Name { get; }
        public string ClassName { get; }
        public HashSet<string> Roles { get; } = new(StringComparer.OrdinalIgnoreCase);

        public QuestContextObject Build() => new(
            Name,
            ClassName,
            Roles.OrderBy(value => value, StringComparer.OrdinalIgnoreCase).ToArray());
    }
}
