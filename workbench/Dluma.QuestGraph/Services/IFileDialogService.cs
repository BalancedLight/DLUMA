namespace Dluma.QuestGraph.Services;

public interface IFileDialogService
{
    string? SelectQuestToOpen();
    string? SelectQuestSavePath(string suggestedName);
    string? SelectCatalogRoot();
}
