using System.Collections.ObjectModel;
using Dluma.QuestGraph.Core;

namespace Dluma.QuestGraph.ViewModels;

public sealed class QuestGroupViewModel : ObservableObject
{
    private readonly Action<QuestGroupViewModel>? _layoutChanging;
    private string _title;
    private double _x;
    private double _y;
    private double _width;
    private double _height;

    public QuestGroupViewModel(
        string id,
        string title,
        double x,
        double y,
        double width,
        double height,
        IEnumerable<string> memberStableIds,
        Action<QuestGroupViewModel>? layoutChanging)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        Id = id;
        _title = string.IsNullOrWhiteSpace(title) ? "Group" : title;
        _x = x;
        _y = y;
        _width = Math.Max(1, width);
        _height = Math.Max(1, height);
        _layoutChanging = layoutChanging;
        MemberStableIds = new ObservableCollection<string>(
            memberStableIds.Distinct(StringComparer.Ordinal));
    }

    public string Id { get; }
    public ObservableCollection<string> MemberStableIds { get; }
    public int MemberCount => MemberStableIds.Count;
    public string MemberSummary => MemberStableIds.Count == 1
        ? "1 node"
        : $"{MemberStableIds.Count} nodes";

    public string Title
    {
        get => _title;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? "Group" : value.Trim();
            if (string.Equals(_title, normalized, StringComparison.Ordinal))
                return;
            _layoutChanging?.Invoke(this);
            SetProperty(ref _title, normalized);
        }
    }

    public double X
    {
        get => _x;
        set => SetFinitePositiveOrPosition(ref _x, value, nameof(X), positive: false);
    }

    public double Y
    {
        get => _y;
        set => SetFinitePositiveOrPosition(ref _y, value, nameof(Y), positive: false);
    }

    public double Width
    {
        get => _width;
        set => SetFinitePositiveOrPosition(ref _width, value, nameof(Width), positive: true);
    }

    public double Height
    {
        get => _height;
        set => SetFinitePositiveOrPosition(ref _height, value, nameof(Height), positive: true);
    }

    public bool Contains(string stableId) =>
        MemberStableIds.Contains(stableId, StringComparer.Ordinal);

    public void AddMember(string stableId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(stableId);
        if (Contains(stableId))
            return;
        _layoutChanging?.Invoke(this);
        MemberStableIds.Add(stableId);
        NotifyMembershipChanged();
    }

    public void RemoveMember(string stableId)
    {
        var match = MemberStableIds.FirstOrDefault(member =>
            string.Equals(member, stableId, StringComparison.Ordinal));
        if (match is null)
            return;
        _layoutChanging?.Invoke(this);
        MemberStableIds.Remove(match);
        NotifyMembershipChanged();
    }

    public QuestGraphGroupLayout ToLayout() => new(
        Id,
        Title,
        X,
        Y,
        Width,
        Height,
        MemberStableIds.ToArray());

    private void SetFinitePositiveOrPosition(
        ref double field,
        double value,
        string propertyName,
        bool positive)
    {
        if (!double.IsFinite(value) || (positive && value <= 0) || field.Equals(value))
            return;
        _layoutChanging?.Invoke(this);
        if (SetProperty(ref field, value, propertyName))
            OnPropertyChanged(nameof(ToLayout));
    }

    private void NotifyMembershipChanged()
    {
        OnPropertyChanged(nameof(MemberCount));
        OnPropertyChanged(nameof(MemberSummary));
    }
}
