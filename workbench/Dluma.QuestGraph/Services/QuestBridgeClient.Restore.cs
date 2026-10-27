using Dluma.QuestGraph.Protocol;

namespace Dluma.QuestGraph.Services;

public sealed partial class QuestBridgeClient
{
    public Task<RestoreDocumentResult> RestoreDocumentAsync(
        RestoreDocumentRequest request,
        CancellationToken cancellationToken = default) =>
        RequestAsync<RestoreDocumentRequest, RestoreDocumentResult>(
            "restoreDocument",
            request,
            TimeSpan.FromSeconds(15),
            cancellationToken);
}
