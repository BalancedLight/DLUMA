namespace Dluma.QuestGraph.Core;

public enum QuestCatalogFunctionalCategory
{
    FlowAndContainers,
    ObjectivesAndGuards,
    WorldAndObjects,
    AiAndMovement,
    DialogueAndCinematics,
    PlayerInventoryAndRewards,
    ChallengesAndModes,
    ChildRecords,
    CustomAndOther,
    SupportingDefinition
}

public enum QuestCatalogChildSlotKind
{
    OrderedBody,
    Branch,
    Child
}

public sealed record QuestCatalogStyle(
    string? HeaderColor,
    string? FontColor,
    string? ShortName);

public sealed record QuestCatalogChildSlot(
    string Id,
    string DisplayName,
    QuestCatalogChildSlotKind Kind,
    string Quantity,
    string Template,
    IReadOnlyList<string> AcceptedEntryNames)
{
    public string PortId => Kind switch
    {
        QuestCatalogChildSlotKind.OrderedBody => "body",
        QuestCatalogChildSlotKind.Branch => "branch:" + Id,
        _ => "child:" + Id
    };
}
