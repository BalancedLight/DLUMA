namespace Dluma.QuestGraph.ViewModels;

public sealed partial class QuestDocumentViewModel
{
    // Also used by accessibility surfaces that cannot consume ICommand's
    // async-void Execute contract directly.
    public Task DeleteSelectionAsync() => DeleteSelectedAsync();
}
