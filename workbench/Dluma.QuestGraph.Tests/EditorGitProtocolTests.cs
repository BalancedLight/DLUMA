using Dluma.QuestGraph.Protocol;
using System.Text.Json;
using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class EditorGitProtocolTests
{
    private static string Project => Path.GetFullPath(Path.Combine(Path.GetTempPath(), "EditorGitProject"));
    private const string Lease = "0123456789abcdef0123456789abcdef";

    [Fact]
    public void LocalDiagnosticWritesRequireMutationAdmissionWithoutWorktreeWrites()
    {
        QuestBridgePayloadValidator.Validate(new GitContextMessage(Project, 1, true, true, false, "", true));
        QuestBridgePayloadValidator.Validate(new GitContextMessage(Project, 1, true, true, false, "", true, true, false));
        QuestBridgePayloadValidator.Validate(new GitContextMessage(Project, 1, true, false, false, ""));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(
            new GitContextMessage(Project, 1, true, false, false, "", true)));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(
            new GitContextMessage(Project, 1, true, true, true, "", true)));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(
            new GitContextMessage(Project, 1, true, true, false, "", true, true, true)));
        var options = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
        var legacyJson = JsonSerializer.Serialize(new { projectRoot = Project, projectGeneration = 1UL,
            enabled = true, mutationsAvailable = false, worktreeAvailable = false, message = "" });
        var legacy = JsonSerializer.Deserialize<GitContextMessage>(legacyJson, options);
        Assert.NotNull(legacy);
        Assert.False(legacy.LocalWritesOnly);
        QuestBridgePayloadValidator.Validate(legacy);
    }

    [Fact]
    public void ValidProjectAndWorktreeRequestsUseTheSharedValidator()
    {
        QuestBridgePayloadValidator.Validate(new GitProjectAcquireRequest(Project, 1, "stage"));
        QuestBridgePayloadValidator.Validate(new GitWorktreeAcquireRequest(Project, 1, "pull", Lease));
    }

    [Theory]
    [InlineData("reset")]
    [InlineData("forcePush")]
    [InlineData("")]
    public void UnknownMutationIsRejected(string operation) =>
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitProjectAcquireRequest(Project, 1, operation)));

    [Fact]
    public void ProjectGenerationAndRootAreRequired()
    {
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitProjectAcquireRequest(Project, 0, "stage")));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitProjectAcquireRequest("relative", 1, "stage")));
    }

    [Fact]
    public void WorktreeChangeRequiresItsProjectLeaseAndOperation()
    {
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitWorktreeAcquireRequest(Project, 1, "pull", "wrong")));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitWorktreeAcquireRequest(Project, 1, "stage", Lease)));
    }

    [Fact]
    public void MutatingChildRequiresLeaseAndVerifiableProcessIdentity()
    {
        QuestBridgePayloadValidator.Validate(new GitChildStartedRequest(Lease, 42, 123, 64, true));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitChildStartedRequest(null, 42, 123, 64, true)));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitChildStartedRequest(Lease, 42, 0, 64, true)));
    }

    [Fact]
    public void NativeCommandsCannotSelectMetadataOrParentPaths()
    {
        QuestBridgePayloadValidator.Validate(new GitCommandMessage("stage", ["data/quest.pxsl", "-name.txt"]));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitCommandMessage("stage", ["../outside.txt"])));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitCommandMessage("stage", [".git/config"])));
    }

    [Fact]
    public void LeaseReplyCannotGrantAnEmptyLease()
    {
        QuestBridgePayloadValidator.Validate(new GitLeaseResult(true, Lease, "ok", ""));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitLeaseResult(true, null, "ok", "")));
    }

    [Fact]
    public void PendingProjectArmHasSeparateValidatedIdentity()
    {
        QuestBridgePayloadValidator.Validate(new GitProjectAcquireRequest(Project, 1, "stage", null, Lease));
        QuestBridgePayloadValidator.Validate(new GitProjectCancelRequest(Project, 1, Lease));
        QuestBridgePayloadValidator.Validate(new GitLeaseResult(false, null, "pending", "", Lease));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(new GitProjectCancelRequest(Project, 1, "bad")));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(new GitLeaseResult(false, null, "pending", "", null)));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(new GitLeaseResult(true, Lease, "ok", "", Lease)));
        Assert.Throws<InvalidDataException>(() => QuestBridgePayloadValidator.Validate(new GitLeaseResult(false, null, "unavailable", "", Lease)));
    }
    [Fact]
    public void ChildRegistrationCannotSucceedWithUnresolvedRecovery()
    {
        QuestBridgePayloadValidator.Validate(new GitChildStartedResult(true, false, "ok", ""));
        Assert.Throws<InvalidDataException>(() =>
            QuestBridgePayloadValidator.Validate(new GitChildStartedResult(true, true, "ok", "")));
    }
}
