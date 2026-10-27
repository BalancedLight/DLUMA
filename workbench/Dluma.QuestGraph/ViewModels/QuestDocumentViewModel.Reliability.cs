using Dluma.QuestGraph.Core;
using Dluma.QuestGraph.Protocol;

namespace Dluma.QuestGraph.ViewModels;

public sealed record QuestSourceRestoreBaseline(
    ReadOnlyMemory<byte> Bytes,
    string Sha256,
    bool WasModified);

public sealed partial class QuestDocumentViewModel
{
    private bool _savedWasModified;

    public Func<
        QuestDocumentViewModel,
        QuestSourceRestoreBaseline,
        Task<RestoreDocumentResult?>>? ConnectedRestoreHandler { get; set; }

    public void ReacquireConnectedSnapshot(
        OpenDocumentMessage message,
        QuestCatalog? catalog,
        string projectRoot)
    {
        if (!RequiresConnection)
            throw new InvalidOperationException("An offline PXSL document cannot be rebound to a Developer Tools buffer.");
        QuestBridgePayloadValidator.Validate(message);
        if (!string.Equals(
                ConnectedProjectRoot,
                Path.GetFullPath(projectRoot),
                StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The refreshed Developer Tools buffer belongs to a different project.");
        if (!SameRelativePath(RelativePath, message.RelativePath))
            throw new InvalidDataException("The refreshed Developer Tools buffer represents a different PXSL document.");

        var snapshot = new QuestSourceSnapshot(
            message.DocumentId,
            message.RelativePath,
            message.Revision,
            message.Bytes,
            message.EncodingName);
        if (!string.Equals(snapshot.Sha256, message.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The refreshed Developer Tools bytes do not match their source identity.");

        var layout = CreateSidecar();
        _catalog = catalog;
        _model = PxslParser.Parse(snapshot, _catalog);
        RebuildGraph(layout);
        MarkConnected(true);
        NotifyModelChanged();
        Status = "Reacquired the refreshed Developer Tools PXSL buffer.";
    }

    private async Task<(bool Admitted, long? Revision)> AdmitSavedBaselineAsync(
        DocumentState target)
    {
        if (!RequiresConnection)
            return (true, null);
        if (ConnectedRestoreHandler is null)
            return (false, null);

        var baseline = new QuestSourceRestoreBaseline(
            (byte[])target.Bytes.Clone(),
            _savedSourceSha256,
            _savedWasModified);
        try
        {
            var result = await ConnectedRestoreHandler(this, baseline).ConfigureAwait(true);
            if (result is null)
            {
                // The native document disappeared. Restoring the managed projection is
                // still safe because the caller is closing this companion document.
                return (true, null);
            }
            if (!result.Restored)
            {
                Status = result.Message;
                return (false, null);
            }
            if (!string.Equals(result.Sha256, baseline.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Developer Tools restored bytes with a different source identity.");
            return (true, result.Revision);
        }
        catch (Exception exception) when (exception is
            IOException or InvalidDataException or InvalidOperationException or
            OperationCanceledException or System.Text.Json.JsonException)
        {
            Status = $"Developer Tools could not restore the saved PXSL baseline ({exception.GetType().Name}).";
            return (false, null);
        }
    }

    private static bool SameRelativePath(string left, string right) =>
        string.Equals(
            left.Replace('\\', '/').TrimStart('/'),
            right.Replace('\\', '/').TrimStart('/'),
            StringComparison.OrdinalIgnoreCase);
}
