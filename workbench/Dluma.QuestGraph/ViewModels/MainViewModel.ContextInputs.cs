namespace Dluma.QuestGraph.ViewModels;

public sealed partial class MainViewModel
{
    private string _contextObjectInput = string.Empty;
    private string _contextTextInput = string.Empty;
    private string _speakerInput = string.Empty;
    private string _dialogueGroupInput = string.Empty;
    private string _questReferenceInput = string.Empty;

    public string ContextObjectInput
    {
        get => _contextObjectInput;
        set => SetProperty(ref _contextObjectInput, value);
    }

    public string ContextTextInput
    {
        get => _contextTextInput;
        set => SetProperty(ref _contextTextInput, value);
    }

    public string SpeakerInput
    {
        get => _speakerInput;
        set => SetProperty(ref _speakerInput, value);
    }

    public string DialogueGroupInput
    {
        get => _dialogueGroupInput;
        set => SetProperty(ref _dialogueGroupInput, value);
    }

    public string QuestReferenceInput
    {
        get => _questReferenceInput;
        set => SetProperty(ref _questReferenceInput, value);
    }
}
