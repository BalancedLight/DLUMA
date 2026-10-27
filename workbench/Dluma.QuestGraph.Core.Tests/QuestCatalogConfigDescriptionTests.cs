using System.Text;
using Dluma.QuestGraph.Core;
using Xunit;

namespace Dluma.QuestGraph.Core.Tests;

public sealed class QuestCatalogConfigDescriptionTests
{
    [Fact]
    public void DescArgumentsPreserveSignatureAndIndentedChildTemplate()
    {
        using var edf = new MemoryStream(Encoding.UTF8.GetBytes("""
            ### SimplePhase ###
            ,enable-multi name =
              Phase -type=enable -name=<(,name)>
            """), writable: false);
        using var config = new MemoryStream(Encoding.UTF8.GetBytes("""
            Descriptions()
                Desc(",enable-multi", ",enable-multi [phase_name]", "    ,o [objects_class] [object_name]", "")
            """), writable: false);

        var catalog = QuestCatalogExtractor.Extract(
        [
            new("macros.edf", QuestCatalogFormat.Edf, edf),
            new("QuestEditorConfig.scr", QuestCatalogFormat.QuestEditorConfig, config)
        ]);
        var entry = Assert.Single(catalog.Entries,
            entry => entry.Kind == QuestCatalogEntryKind.Macro && entry.Name == "enable-multi");

        Assert.Equal(
            new[]
            {
                ",enable-multi [phase_name]",
                "    ,o [objects_class] [object_name]"
            },
            entry.DescriptionLines);
    }
}
