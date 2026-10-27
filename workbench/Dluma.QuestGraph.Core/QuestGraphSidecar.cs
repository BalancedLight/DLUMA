using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dluma.QuestGraph.Core;

public sealed record QuestGraphSourceIdentity(
    string RelativePath,
    string Sha256,
    int Length);

public sealed record QuestNodeAnchor(
    int SourceStart,
    int SourceLength,
    string SourceTextSha256,
    string SemanticFingerprint,
    string ContainerFingerprint);

public sealed record QuestGraphPoint(double X, double Y);

public sealed record QuestGraphViewport(double X, double Y, double Zoom)
{
    public static QuestGraphViewport Default { get; } = new(0, 0, 1);
}

public sealed record QuestGraphNodeLayout(
    string StableId,
    QuestNodeAnchor Anchor,
    double X,
    double Y,
    double Width,
    double Height,
    bool IsCollapsed,
    string? GroupId,
    string? Note);

public sealed record QuestGraphGroupLayout(
    string Id,
    string Title,
    double X,
    double Y,
    double Width,
    double Height,
    IReadOnlyList<string> MemberStableIds);

public sealed record QuestGraphSidecarV1
{
    public const string SchemaId = "dluma.quest_graph.v1";

    public string Schema { get; init; } = SchemaId;
    public string DocumentId { get; init; } = string.Empty;
    public QuestGraphSourceIdentity Source { get; init; } = new(string.Empty, string.Empty, 0);
    public QuestGraphViewport Viewport { get; init; } = QuestGraphViewport.Default;
    public IReadOnlyList<QuestGraphNodeLayout> Nodes { get; init; } = Array.Empty<QuestGraphNodeLayout>();
    public IReadOnlyList<QuestGraphGroupLayout> Groups { get; init; } = Array.Empty<QuestGraphGroupLayout>();
}

public enum QuestGraphAnchorMatchKind
{
    ExactAnchor,
    UniqueSemanticFingerprint
}

public sealed record QuestGraphLayoutMatch(
    QuestGraphNodeLayout Layout,
    QuestSemanticNode SemanticNode,
    QuestGraphAnchorMatchKind Kind);

public sealed record QuestGraphReconciliationResult(
    IReadOnlyList<QuestGraphLayoutMatch> Matches,
    IReadOnlyList<QuestGraphNodeLayout> Orphans);

public static class QuestGraphSidecarService
{
    public const long MaximumSidecarBytes = 32L * 1024 * 1024;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = false,
        WriteIndented = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };

    public static QuestGraphSidecarV1 Create(
        QuestGraphDocument document,
        IEnumerable<QuestGraphNodeLayout>? nodes = null,
        QuestGraphViewport? viewport = null,
        IEnumerable<QuestGraphGroupLayout>? groups = null)
    {
        ArgumentNullException.ThrowIfNull(document);
        var sidecar = new QuestGraphSidecarV1
        {
            DocumentId = document.Source.DocumentId,
            Source = new QuestGraphSourceIdentity(
                document.Source.RelativePath,
                document.Source.Sha256,
                document.Source.Bytes.Length),
            Viewport = viewport ?? QuestGraphViewport.Default,
            Nodes = nodes?.ToArray() ?? Array.Empty<QuestGraphNodeLayout>(),
            Groups = groups?.ToArray() ?? Array.Empty<QuestGraphGroupLayout>()
        };
        Validate(sidecar);
        return sidecar;
    }

    public static QuestGraphNodeLayout CreateNodeLayout(
        QuestSemanticNode node,
        string stableId,
        double x,
        double y,
        double width = 260,
        double height = 140,
        bool isCollapsed = false,
        string? groupId = null,
        string? note = null)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentException.ThrowIfNullOrWhiteSpace(stableId);
        return new QuestGraphNodeLayout(
            stableId,
            new QuestNodeAnchor(
                node.Span.Start,
                node.Span.Length,
                node.SourceTextSha256,
                node.SemanticFingerprint,
                node.ContainerFingerprint),
            x,
            y,
            width,
            height,
            isCollapsed,
            groupId,
            note);
    }

    public static QuestGraphReconciliationResult Reconcile(
        QuestGraphSidecarV1 sidecar,
        QuestGraphDocument document)
    {
        ArgumentNullException.ThrowIfNull(sidecar);
        ArgumentNullException.ThrowIfNull(document);
        Validate(sidecar);

        var matches = new List<QuestGraphLayoutMatch>();
        var orphans = new List<QuestGraphNodeLayout>();
        var assigned = new HashSet<string>(StringComparer.Ordinal);
        foreach (var layout in sidecar.Nodes)
        {
            var exact = document.SemanticNodes
                .Where(node => node.Span.Start == layout.Anchor.SourceStart &&
                    node.Span.Length == layout.Anchor.SourceLength &&
                    string.Equals(
                        node.SourceTextSha256,
                        layout.Anchor.SourceTextSha256,
                        StringComparison.OrdinalIgnoreCase))
                .Where(node => !assigned.Contains(node.Id))
                .ToArray();
            if (exact.Length == 1)
            {
                assigned.Add(exact[0].Id);
                matches.Add(new QuestGraphLayoutMatch(
                    layout,
                    exact[0],
                    QuestGraphAnchorMatchKind.ExactAnchor));
                continue;
            }

            var semantic = document.SemanticNodes
                .Where(node =>
                    string.Equals(
                        node.SemanticFingerprint,
                        layout.Anchor.SemanticFingerprint,
                        StringComparison.OrdinalIgnoreCase) &&
                    string.Equals(
                        node.ContainerFingerprint,
                        layout.Anchor.ContainerFingerprint,
                        StringComparison.OrdinalIgnoreCase))
                .Where(node => !assigned.Contains(node.Id))
                .ToArray();
            if (semantic.Length == 1)
            {
                assigned.Add(semantic[0].Id);
                matches.Add(new QuestGraphLayoutMatch(
                    layout,
                    semantic[0],
                    QuestGraphAnchorMatchKind.UniqueSemanticFingerprint));
            }
            else
            {
                orphans.Add(layout);
            }
        }

        return new QuestGraphReconciliationResult(matches, orphans);
    }

    public static async Task WriteAsync(
        Stream stream,
        QuestGraphSidecarV1 sidecar,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(sidecar);
        Validate(sidecar);
        await JsonSerializer.SerializeAsync(stream, sidecar, JsonOptions, cancellationToken);
    }

    public static async Task<QuestGraphSidecarV1> ReadAsync(
        Stream stream,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (stream.CanSeek &&
            stream.Length - stream.Position > MaximumSidecarBytes)
            throw new InvalidDataException("Quest graph sidecar exceeds the 32 MiB size limit.");
        var sidecar = await JsonSerializer.DeserializeAsync<QuestGraphSidecarV1>(
            stream,
            JsonOptions,
            cancellationToken) ?? throw new InvalidDataException("Quest graph sidecar is empty.");
        Validate(sidecar);
        return sidecar;
    }

    public static async Task SaveFileAsync(
        string path,
        QuestGraphSidecarV1 sidecar,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(sidecar);
        Validate(sidecar);
        var fullPath = Path.GetFullPath(path);
        var directory = Path.GetDirectoryName(fullPath) ??
            throw new InvalidOperationException("Quest graph sidecar path has no parent directory.");
        Directory.CreateDirectory(directory);
        var temporary = Path.Combine(
            directory,
            $".{Path.GetFileName(path)}.{Guid.NewGuid():N}.tmp");
        try
        {
            await using (var stream = new FileStream(
                temporary,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None,
                4096,
                FileOptions.Asynchronous | FileOptions.WriteThrough))
            {
                await WriteAsync(stream, sidecar, cancellationToken);
                await stream.FlushAsync(cancellationToken);
            }
            File.Move(temporary, fullPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    public static async Task<QuestGraphSidecarV1?> LoadFileAsync(
        string path,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        if (!File.Exists(path))
            return null;
        var information = new FileInfo(path);
        if (information.Length > MaximumSidecarBytes)
            throw new InvalidDataException("Quest graph sidecar exceeds the 32 MiB size limit.");
        await using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            4096,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        return await ReadAsync(stream, cancellationToken);
    }

    public static void Validate(QuestGraphSidecarV1 sidecar)
    {
        if (sidecar is null)
            throw new InvalidDataException("Quest graph sidecar cannot be null.");
        if (!string.Equals(sidecar.Schema, QuestGraphSidecarV1.SchemaId, StringComparison.Ordinal))
            throw new InvalidDataException($"Unsupported quest graph sidecar schema '{sidecar.Schema}'.");
        if (string.IsNullOrWhiteSpace(sidecar.DocumentId))
            throw new InvalidDataException("Quest graph document identity cannot be blank.");
        if (sidecar.Source is null)
            throw new InvalidDataException("Quest graph source identity is missing.");
        if (string.IsNullOrWhiteSpace(sidecar.Source.RelativePath) ||
            sidecar.Source.RelativePath.Contains('\0') ||
            Path.IsPathRooted(sidecar.Source.RelativePath) ||
            sidecar.Source.RelativePath.Replace('\\', '/').Split('/').Contains("..", StringComparer.Ordinal))
            throw new InvalidDataException("Quest graph source identity must use a safe relative path.");
        if (sidecar.Source.Length < 0 || !IsSha256(sidecar.Source.Sha256))
            throw new InvalidDataException("Quest graph source identity is invalid.");
        if (sidecar.Viewport is null)
            throw new InvalidDataException("Quest graph viewport is missing.");
        if (!double.IsFinite(sidecar.Viewport.X) ||
            !double.IsFinite(sidecar.Viewport.Y) ||
            !double.IsFinite(sidecar.Viewport.Zoom) ||
            sidecar.Viewport.Zoom <= 0)
            throw new InvalidDataException("Quest graph viewport is invalid.");
        if (sidecar.Nodes is null || sidecar.Groups is null)
            throw new InvalidDataException("Quest graph sidecar collections cannot be null.");
        if (sidecar.Nodes.Count > 100_000 || sidecar.Groups.Count > 10_000)
            throw new InvalidDataException("Quest graph sidecar exceeds collection limits.");
        var nodeIds = new HashSet<string>(StringComparer.Ordinal);
        var nodesById = new Dictionary<string, QuestGraphNodeLayout>(StringComparer.Ordinal);
        foreach (var node in sidecar.Nodes)
        {
            if (node is null || string.IsNullOrWhiteSpace(node.StableId) ||
                !nodeIds.Add(node.StableId))
                throw new InvalidDataException("Quest graph node stable IDs must be nonblank and unique.");
            nodesById.Add(node.StableId, node);
            if (node.Anchor is null)
                throw new InvalidDataException($"Quest graph node anchor '{node.StableId}' is missing.");
            if (node.Anchor.SourceStart < 0 || node.Anchor.SourceLength < 0 ||
                !IsSha256(node.Anchor.SourceTextSha256) ||
                !IsSha256(node.Anchor.SemanticFingerprint) ||
                string.IsNullOrWhiteSpace(node.Anchor.ContainerFingerprint))
                throw new InvalidDataException($"Quest graph node anchor '{node.StableId}' is invalid.");
            if (!double.IsFinite(node.X) || !double.IsFinite(node.Y) ||
                !double.IsFinite(node.Width) || !double.IsFinite(node.Height) ||
                node.Width <= 0 || node.Height <= 0)
                throw new InvalidDataException($"Quest graph node layout '{node.StableId}' is invalid.");
        }

        var groupIds = new HashSet<string>(StringComparer.Ordinal);
        var membersByGroup = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal);
        var assignedMembers = new HashSet<string>(StringComparer.Ordinal);
        foreach (var group in sidecar.Groups)
        {
            if (group is null || string.IsNullOrWhiteSpace(group.Id) ||
                !groupIds.Add(group.Id))
                throw new InvalidDataException("Quest graph group IDs must be nonblank and unique.");
            if (group.Title is null || group.MemberStableIds is null ||
                !double.IsFinite(group.X) || !double.IsFinite(group.Y) ||
                !double.IsFinite(group.Width) || !double.IsFinite(group.Height) ||
                group.Width <= 0 || group.Height <= 0)
                throw new InvalidDataException($"Quest graph group '{group.Id}' is invalid.");
            var members = new HashSet<string>(StringComparer.Ordinal);
            foreach (var memberId in group.MemberStableIds)
            {
                if (string.IsNullOrWhiteSpace(memberId) || !members.Add(memberId))
                    throw new InvalidDataException(
                        $"Quest graph group '{group.Id}' member IDs must be nonblank and unique.");
                if (!nodesById.ContainsKey(memberId))
                    throw new InvalidDataException(
                        $"Quest graph group '{group.Id}' references a missing node.");
                if (!assignedMembers.Add(memberId))
                    throw new InvalidDataException(
                        $"Quest graph node '{memberId}' belongs to more than one group.");
            }
            membersByGroup.Add(group.Id, members);
        }

        foreach (var node in sidecar.Nodes)
        {
            if (node.GroupId is null)
            {
                if (assignedMembers.Contains(node.StableId))
                    throw new InvalidDataException(
                        $"Quest graph node '{node.StableId}' group membership is inconsistent.");
                continue;
            }
            if (string.IsNullOrWhiteSpace(node.GroupId) ||
                !membersByGroup.TryGetValue(node.GroupId, out var members) ||
                !members.Contains(node.StableId))
                throw new InvalidDataException(
                    $"Quest graph node '{node.StableId}' references an inconsistent group.");
        }
    }

    private static bool IsSha256(string? value) =>
        value is { Length: 64 } &&
        value.All(character =>
            character is >= '0' and <= '9' or
                >= 'a' and <= 'f' or
                >= 'A' and <= 'F');
}
