using Dluma.QuestGraph.Protocol;
using System.Windows.Threading;

namespace Dluma.QuestGraph.ViewModels;

public sealed partial class MainViewModel
{
    private async Task<RestoreDocumentResult?> RestoreConnectedBaselineAsync(
        QuestDocumentViewModel document,
        QuestSourceRestoreBaseline baseline)
    {
        if (!string.Equals(
                document.ConnectedProjectRoot,
                _projectRoot,
                StringComparison.OrdinalIgnoreCase))
        {
            document.MarkConnected(false);
            return null;
        }
        const int maximumAttempts = 2;
        for (var attempt = 0; attempt < maximumAttempts; ++attempt)
        {
            if (!_bridge.IsConnected)
            {
                document.MarkConnected(false);
                return null;
            }

            var requestedDocumentId = document.DocumentId;
            try
            {
                var request = new RestoreDocumentRequest(
                    requestedDocumentId,
                    baseline.Bytes.ToArray(),
                    baseline.Sha256,
                    baseline.WasModified);
                return await _bridge.RestoreDocumentAsync(request).ConfigureAwait(true);
            }
            catch (QuestBridgeException exception) when (
                string.Equals(
                    exception.Code,
                    "document_not_active",
                    StringComparison.OrdinalIgnoreCase))
            {
                // A dialog refresh can replace its native document identity while
                // retaining the same admitted project-relative PXSL path. Give the
                // queued openDocument notification a turn, then retry only if it
                // actually rebound this view model to the refreshed identity.
                await App.Current.Dispatcher.InvokeAsync(
                    static () => { },
                    DispatcherPriority.Background);
                if (!string.Equals(
                        requestedDocumentId,
                        document.DocumentId,
                        StringComparison.Ordinal) &&
                    attempt + 1 < maximumAttempts)
                    continue;

                document.MarkConnected(false);
                document.Status = "The Developer Tools PXSL document closed before its baseline could be restored; the companion can close without touching another buffer.";
                return null;
            }
            catch (QuestBridgeException exception) when (
                exception.Retryable && attempt + 1 < maximumAttempts)
            {
                await App.Current.Dispatcher.InvokeAsync(
                    static () => { },
                    DispatcherPriority.Background);
            }
            catch (Exception exception) when (
                (exception is IOException or InvalidOperationException) &&
                !_bridge.IsConnected)
            {
                document.MarkConnected(false);
                return null;
            }
            catch (QuestBridgeException exception)
            {
                document.Status = $"Developer Tools rejected source restoration at {exception.Code}.";
                return new RestoreDocumentResult(
                    false,
                    document.Revision,
                    document.SourceSha256,
                    document.Status);
            }
            catch (Exception exception) when (exception is InvalidDataException or InvalidOperationException)
            {
                document.Status = $"Developer Tools returned an invalid restoration result ({exception.GetType().Name}).";
                return new RestoreDocumentResult(
                    false,
                    document.Revision,
                    document.SourceSha256,
                    document.Status);
            }
        }

        document.Status = "Developer Tools did not accept restoration of the saved PXSL baseline.";
        return new RestoreDocumentResult(
            false,
            document.Revision,
            document.SourceSha256,
            document.Status);
    }
}
