using Microsoft.Win32;

namespace Dluma.QuestGraph.Services;

public sealed class WpfFileDialogService : IFileDialogService
{
    public string? SelectQuestToOpen()
    {
        var dialog = new OpenFileDialog
        {
            Title = "Open PXSL quest",
            Filter = "PXSL quest (*.pxsl)|*.pxsl|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    public string? SelectQuestSavePath(string suggestedName)
    {
        var dialog = new SaveFileDialog
        {
            Title = "Save PXSL quest copy",
            Filter = "PXSL quest (*.pxsl)|*.pxsl|All files (*.*)|*.*",
            DefaultExt = ".pxsl",
            AddExtension = true,
            FileName = suggestedName
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    public string? SelectCatalogRoot()
    {
        var dialog = new OpenFolderDialog
        {
            Title = "Select quest definition root",
            Multiselect = false
        };
        return dialog.ShowDialog() == true ? dialog.FolderName : null;
    }
}
