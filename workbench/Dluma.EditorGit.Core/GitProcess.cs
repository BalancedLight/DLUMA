using System.Diagnostics;
using System.Text;

namespace Dluma.EditorGit.Core;

internal sealed class GitProcess(string executable, Func<bool> isQuarantined, Action quarantine,
    IGitProcessObserver? observer = null)
{
    private const int MaxOutput = 16 * 1024 * 1024;
    private static readonly TimeSpan LocalTimeout = TimeSpan.FromSeconds(30);
    private static readonly TimeSpan NetworkTimeout = TimeSpan.FromMinutes(5);
    private readonly string _executable = !string.IsNullOrWhiteSpace(executable) && Path.IsPathFullyQualified(executable) && File.Exists(executable)
        ? Path.GetFullPath(executable) : throw new GitException("Select an installed Git executable.", GitFailureKind.GitUnavailable, "git");

    internal async Task<byte[]> RunAsync(string directory, IReadOnlyList<string> args, bool network, CancellationToken token, byte[]? input = null)
    {
        var commandIndex = 0;
        while (commandIndex < args.Count && args[commandIndex].StartsWith('-'))
            commandIndex += args[commandIndex] is "-c" or "-C" ? 2 : 1;
        var operation = commandIndex < args.Count ? args[commandIndex] : "git";
        var remoteSubcommand = commandIndex + 1 < args.Count ? args[commandIndex + 1] : null;
        var mayChangeState = operation is "fetch" or "pull" or "push" or "clone" or "init" or "commit" or "branch" or "switch" or "add" or "restore" or "update-index" ||
            operation == "remote" && remoteSubcommand is "add" or "remove" or "rename" or "set-url";
        if (isQuarantined()) throw new GitException("A previous Git process may still be running. Restart the Editor after checking it has exited.", GitFailureKind.RecoveryRequired, operation);
        var start = new ProcessStartInfo(_executable)
        {
            WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true
        };
        start.Environment["GIT_TERMINAL_PROMPT"] = "0";
        // A launcher environment must not redirect commands to another repository or index.
        foreach (var name in new[] { "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES" })
            start.Environment.Remove(name);
        foreach (var arg in args) start.ArgumentList.Add(arg);
        GitSuspendedProcess child;
        try
        {
            token.ThrowIfCancellationRequested();
            child = GitSuspendedProcess.Start(start, quarantine);
        }
        catch (System.ComponentModel.Win32Exception)
        {
            throw new GitException("Git could not be started.", GitFailureKind.GitUnavailable, operation);
        }
        using var owned = child;
        var process = child.Process;
        Task write = Task.CompletedTask;
        var output = ReadBoundedAsync(child.Output);
        var error = ReadBoundedAsync(child.Error);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(network ? NetworkTimeout : LocalTimeout);
        (byte[] Bytes, bool Overflow)[] streams;
        var resumeAttempted = false;
        try
        {
            if (observer is not null)
            {
                // The original CreateProcess handle remains owned through native acknowledgement.
                await observer.StartedAsync(new(process.Id, child.CreationTime,
                    child.ProcessHandle, mayChangeState), timeout.Token).ConfigureAwait(false);
            }
            timeout.Token.ThrowIfCancellationRequested();
            resumeAttempted = true;
            child.Resume();
            write = WriteInputAsync(child.Input, input);
            await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
            await write.WaitAsync(timeout.Token).ConfigureAwait(false);
            streams = await Task.WhenAll(output, error).WaitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (Exception failure)
        {
            try { process.Kill(entireProcessTree: true); } catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
            using var cleanup = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await process.WaitForExitAsync(cleanup.Token).ConfigureAwait(false);
                await Task.WhenAll(output, error).WaitAsync(cleanup.Token).ConfigureAwait(false);
                try { await write.WaitAsync(cleanup.Token).ConfigureAwait(false); }
                catch (IOException) { /* A closed child may reject pending stdin. */ }
            }
            catch (Exception cleanupFailure) when (cleanupFailure is OperationCanceledException or IOException or
                                                        InvalidOperationException or System.ComponentModel.Win32Exception)
            {
                quarantine();
                throw new GitException("Git did not exit after cancellation. Check for a remaining Git process before retrying.", GitFailureKind.RecoveryRequired, operation, outcomeUncertain: mayChangeState);
            }
            if (failure is OperationCanceledException)
            {
                if (token.IsCancellationRequested)
                    throw new GitException(operation == "push" && resumeAttempted ? "Canceled. The remote outcome may be unknown; refresh before retrying." : "Canceled. Refresh before retrying.", GitFailureKind.Canceled, operation, outcomeUncertain: mayChangeState && resumeAttempted);
                throw new GitException(operation == "push" && resumeAttempted ? "Git timed out. The remote outcome may be unknown; refresh before retrying." : "Git timed out. Refresh before retrying.", GitFailureKind.Timeout, operation, outcomeUncertain: mayChangeState && resumeAttempted);
            }
            if (failure is GitException gitFailure)
                throw new GitException(gitFailure.Message, gitFailure.Kind, gitFailure.Operation ?? operation,
                    gitFailure.ExitCode, gitFailure.OutcomeUncertain || mayChangeState && resumeAttempted);
            throw new GitException("Git could not finish safely. Refresh before retrying.",
                GitFailureKind.CommandFailed, operation, outcomeUncertain: mayChangeState && resumeAttempted);
        }
        if (streams[0].Overflow || streams[1].Overflow)
            throw new GitException("Git returned too much output. Refresh the repository before retrying.", GitFailureKind.CommandFailed, operation, process.ExitCode, network);
        // Do not surface stderr: Git and helpers can echo credential-bearing remote URLs.
        if (process.ExitCode != 0)
        {
            // Porcelain reports a rejected destination with a leading ! and a tab.
            var rejected = operation == "push" && args.Contains("--porcelain") &&
                Encoding.UTF8.GetString(streams[0].Bytes).Split('\n').Any(line => line.StartsWith("!\t", StringComparison.Ordinal));
            if (rejected)
                throw new GitException("The remote rejected the push. Fetch and review its changes before retrying.", GitFailureKind.PushRejected, operation, process.ExitCode);
            throw new GitException(network
                ? "Git could not complete the remote operation. Check the remote, authentication, and repository state."
                : "Git could not complete the operation. Refresh the repository and check its state.",
                GitFailureKind.CommandFailed, operation, process.ExitCode, network);
        }
        return streams[0].Bytes;
    }

    private static async Task WriteInputAsync(Stream stream, byte[]? input)
    {
        try { if (input is not null) await stream.WriteAsync(input).ConfigureAwait(false); }
        finally { await stream.DisposeAsync().ConfigureAwait(false); }
    }

    private static async Task<(byte[] Bytes, bool Overflow)> ReadBoundedAsync(Stream stream)
    {
        using var buffer = new MemoryStream();
        var chunk = new byte[8192];
        var overflow = false;
        int count;
        while ((count = await stream.ReadAsync(chunk).ConfigureAwait(false)) != 0)
        {
            var remaining = MaxOutput - (int)buffer.Length;
            if (remaining > 0) buffer.Write(chunk, 0, Math.Min(remaining, count));
            if (count > remaining) overflow = true;
        }
        return (buffer.ToArray(), overflow);
    }

    internal static string Text(byte[] bytes) => new UTF8Encoding(false, true).GetString(bytes);
}
