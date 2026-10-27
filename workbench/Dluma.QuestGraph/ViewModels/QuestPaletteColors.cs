using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public static class QuestPaletteColors
{
    public const string Default = "#3A566F";

    public static string ForEntry(QuestCatalogEntry? entry) =>
        entry?.Style?.HeaderColor ?? ForCategory(
            entry?.Category ?? QuestCatalogFunctionalCategory.CustomAndOther);

    public static string TextForEntry(QuestCatalogEntry? entry)
    {
        if (!string.IsNullOrWhiteSpace(entry?.Style?.FontColor))
            return entry.Style.FontColor!;
        var color = ForEntry(entry).TrimStart('#');
        if (color.Length == 8)
            color = color[2..];
        if (color.Length != 6 || !int.TryParse(color, System.Globalization.NumberStyles.HexNumber,
                System.Globalization.CultureInfo.InvariantCulture, out var rgb))
            return "#FFFFFF";
        var red = (rgb >> 16) & 0xff;
        var green = (rgb >> 8) & 0xff;
        var blue = rgb & 0xff;
        var luminance = (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0;
        return luminance > 0.58 ? "#101720" : "#FFFFFF";
    }

    public static string DisplayName(QuestCatalogFunctionalCategory category) => category switch
    {
        QuestCatalogFunctionalCategory.FlowAndContainers => "Flow & Containers",
        QuestCatalogFunctionalCategory.ObjectivesAndGuards => "Objectives & Guards",
        QuestCatalogFunctionalCategory.WorldAndObjects => "World & Objects",
        QuestCatalogFunctionalCategory.AiAndMovement => "AI & Movement",
        QuestCatalogFunctionalCategory.DialogueAndCinematics => "Dialogue & Cinematics",
        QuestCatalogFunctionalCategory.PlayerInventoryAndRewards => "Player / Inventory / Rewards",
        QuestCatalogFunctionalCategory.ChallengesAndModes => "Challenges & Modes",
        QuestCatalogFunctionalCategory.ChildRecords => "Child Records",
        _ => "Custom / Other"
    };

    public static string ForCategory(QuestCatalogFunctionalCategory category) =>
        category switch
        {
            QuestCatalogFunctionalCategory.FlowAndContainers => "#3E745B",
            QuestCatalogFunctionalCategory.ObjectivesAndGuards => "#846638",
            QuestCatalogFunctionalCategory.WorldAndObjects => "#3E6D73",
            QuestCatalogFunctionalCategory.AiAndMovement => "#805047",
            QuestCatalogFunctionalCategory.DialogueAndCinematics => "#654D7C",
            QuestCatalogFunctionalCategory.PlayerInventoryAndRewards => "#3C6680",
            QuestCatalogFunctionalCategory.ChallengesAndModes => "#7B5B32",
            QuestCatalogFunctionalCategory.ChildRecords => "#4D6671",
            QuestCatalogFunctionalCategory.SupportingDefinition => "#555D68",
            _ => Default
        };
}
