namespace Dluma.EditorGit
{
    internal sealed record TestHost(int EditorPid);
    internal sealed record GitLaunchOptions(TestHost Host, string ProjectRoot, ulong ProjectGeneration);
}
namespace Dluma.QuestGraph.Services
{
    using Dluma.QuestGraph.Protocol;
    public sealed class QuestBridgeException(string code, string message) : Exception(message) { public string Code { get; } = code; }
    public sealed class QuestBridgeClient
    {
        public int AuthenticatedEditorProcessId => 42;
        public Func<string, object, object> Respond { get; set; } = (_, _) => throw new InvalidOperationException();
        public List<string> Requests { get; } = [];
        public bool Aborted { get; private set; }
        public Task<TResponse> RequestAsync<TRequest, TResponse>(string type, TRequest payload, TimeSpan timeout, CancellationToken token)
        {
            _ = timeout; _ = token;
            Requests.Add(type);
            EditorGitProtocol.Validate((EditorGitPayload)(object)payload!);
            var response = Respond(type, payload!);
            EditorGitProtocol.Validate((EditorGitPayload)response);
            return Task.FromResult((TResponse)response);
        }
        public void AbortProtocolSession(string reason) { _ = reason; Aborted = true; }
    }
}
