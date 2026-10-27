using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestGraphSidecarSafetyTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-sidecar-{Guid.NewGuid():N}");

    [Fact]
    public void InvalidGroupGeometryAndDuplicateMembershipAreRejected()
    {
        var document = Document();
        var nodes = document.SemanticNodes
            .Where(node => node.Kind == QuestSemanticKind.Macro)
            .OrderBy(node => node.Order)
            .ToArray();
        var first = QuestGraphSidecarService.CreateNodeLayout(
            nodes[0], "stable-first", 0, 0, groupId: "group-a");
        var second = QuestGraphSidecarService.CreateNodeLayout(
            nodes[1], "stable-second", 100, 0, groupId: "group-a");
        var validGroup = new QuestGraphGroupLayout(
            "group-a", "Valid", 0, 0, 100, 100,
            new[] { "stable-first", "stable-second" });
        var baseline = QuestGraphSidecarService.Create(
            document,
            new[] { first, second },
            groups: new[] { validGroup });
        var invalidGeometry = baseline with
        {
            Groups = new[]
            {
                new QuestGraphGroupLayout(
                    "group-a", "Invalid", double.NaN, 0, 100, 100,
                    new[] { "stable-first", "stable-second" })
            }
        };
        var duplicateMembership = invalidGeometry with
        {
            Groups = new[]
            {
                new QuestGraphGroupLayout(
                    "group-a", "First", 0, 0, 100, 100,
                    new[] { "stable-first", "stable-second" }),
                new QuestGraphGroupLayout(
                    "group-b", "Second", 0, 0, 100, 100,
                    new[] { "stable-first" })
            }
        };

        Assert.Throws<InvalidDataException>(() =>
            QuestGraphSidecarService.Validate(invalidGeometry));
        Assert.Throws<InvalidDataException>(() =>
            QuestGraphSidecarService.Validate(duplicateMembership));
    }

    [Fact]
    public void MissingGroupReferenceIsRejected()
    {
        var document = Document();
        var first = Assert.Single(document.SemanticNodes,
            node => node.Kind == QuestSemanticKind.Macro && node.Arguments.Contains("first"));
        var layout = QuestGraphSidecarService.CreateNodeLayout(
            first,
            "stable-first",
            0,
            0,
            groupId: "missing-group");
        var sidecar = QuestGraphSidecarService.Create(document) with
        {
            Nodes = new[] { layout }
        };

        Assert.Throws<InvalidDataException>(() =>
            QuestGraphSidecarService.Validate(sidecar));
    }

    [Fact]
    public async Task OversizedSidecarFileIsRejectedBeforeJsonParsing()
    {
        Directory.CreateDirectory(_temporaryRoot);
        var path = Path.Combine(_temporaryRoot, "oversized.pxsl.dlumagraph.json");
        await using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            stream.SetLength(33L * 1024 * 1024);

        await Assert.ThrowsAsync<InvalidDataException>(() =>
            QuestGraphSidecarService.LoadFileAsync(path));
    }

    private static QuestGraphDocument Document()
    {
        var source = new QuestSourceSnapshot(
            "synthetic-document",
            "quests/synthetic.pxsl",
            0,
            Encoding.UTF8.GetBytes(
                "quest Example\n" +
                "    ,step first\n" +
                "    ,step second\n"));
        return PxslParser.Parse(source);
    }

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
