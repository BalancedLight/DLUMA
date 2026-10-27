using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestCatalogColorDiscoveryTests : IDisposable
{
    private readonly string _temporaryRoot = Path.Combine(
        Path.GetTempPath(), $"dluma-quest-colors-{Guid.NewGuid():N}");

    [Fact]
    public void ColorDefsAreDiscoveredAndEnrichResolvedMacroStyle()
    {
        Directory.CreateDirectory(_temporaryRoot);
        Write("macros.edf", """
            ### SimplePhase ###
            ,colored name =
              Phase -type=<<colored phase>> -name=<(,name)>
            """);
        Write("quests_colors.xml", """
            <ColorDefs>
              <PhaseType name="colored phase" color="ABCDEF" font-color="010203" short="clr"/>
            </ColorDefs>
            """);
        var service = new QuestCatalogDiscoveryService();

        var result = service.Discover(
            new[] { new QuestCatalogDiscoveryRoot(_temporaryRoot, QuestCatalogRootScope.ProjectPrivate) });
        var entry = Assert.Single(result.Catalog.InsertableEntries,
            entry => entry.Name == "colored");

        Assert.Contains(result.Inputs, input => input.Format == QuestCatalogFormat.QuestColorsXml);
        Assert.Equal("#ABCDEF", entry.Style!.HeaderColor);
        Assert.Equal("#010203", entry.Style.FontColor);
        Assert.Equal("clr", entry.Style.ShortName);
    }

    private void Write(string name, string text) => File.WriteAllText(
        Path.Combine(_temporaryRoot, name),
        text,
        new UTF8Encoding(false));

    public void Dispose()
    {
        if (Directory.Exists(_temporaryRoot))
            Directory.Delete(_temporaryRoot, recursive: true);
    }
}
