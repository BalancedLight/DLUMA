namespace Dluma.QuestGraph.Core;

public static partial class QuestCatalogExtractor
{
    public static QuestCatalog Extract(IEnumerable<QuestCatalogInput> inputs) =>
        QuestCatalogSemanticsExtractor.Extract(inputs);
}
