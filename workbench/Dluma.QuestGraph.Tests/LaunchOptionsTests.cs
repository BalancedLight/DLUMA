using Xunit;

namespace Dluma.QuestGraph.Tests;

public sealed class LaunchOptionsTests
{
    [Fact]
    public void SmokeTestIsAnExplicitHeadlessLaunch()
    {
        var options = LaunchOptions.Parse(new[] { "--smoke-test" });

        Assert.True(options.SmokeTest);
        Assert.False(options.IsConnectedLaunch);
        Assert.Empty(options.Files);
    }

    [Fact]
    public void ConnectedLaunchRequiresTheCompleteAuthenticatedTuple()
    {
        var options = LaunchOptions.Parse(new[]
        {
            "--pipe", "dluma.questgraph.synthetic",
            "--session-token", new string('a', 32),
            "--editor-pid", "42",
            "--open", "quests/first.pxsl",
            "quests/second.pxsl"
        });

        Assert.True(options.IsConnectedLaunch);
        Assert.False(options.SmokeTest);
        Assert.Equal("dluma.questgraph.synthetic", options.PipeName);
        Assert.Equal(42, options.EditorProcessId);
        Assert.Equal(new[] { "quests/first.pxsl", "quests/second.pxsl" }, options.Files);
    }

    [Theory]
    [MemberData(nameof(HostileArguments))]
    public void HostileOrAmbiguousArgumentsFailClosed(string?[] arguments)
    {
        var sanitized = arguments.Select(argument => argument!).ToArray();
        Assert.Throws<ArgumentException>(() => LaunchOptions.Parse(sanitized));
    }

    [Fact]
    public void NullArgumentCollectionIsRejected()
    {
        Assert.Throws<ArgumentNullException>(() => LaunchOptions.Parse(null!));
    }

    [Fact]
    public void ExcessiveArgumentCountIsRejected()
    {
        var arguments = Enumerable.Repeat("quest.pxsl", 1_025).ToArray();

        Assert.Throws<ArgumentException>(() => LaunchOptions.Parse(arguments));
    }

    [Fact]
    public void ExcessiveCommandLineLengthIsRejected()
    {
        var arguments = new[] { new string('x', 32_768) };

        Assert.Throws<ArgumentException>(() => LaunchOptions.Parse(arguments));
    }

    public static TheoryData<string?[]> HostileArguments => new()
    {
        new string?[] { null },
        new string?[] { string.Empty },
        new string?[] { "   " },
        new string?[] { "--unknown" },
        new string?[] { "--smoke-test=value" },
        new string?[] { "--pipe" },
        new string?[] { "--pipe", "--smoke-test" },
        new string?[] { "--open", "--smoke-test" },
        new string?[] { "--editor-pid", "0" },
        new string?[] { "--editor-pid", "+1" },
        new string?[] { "--editor-pid", "2147483648" },
        new string?[] { "--pipe", "first", "--pipe", "second" },
        new string?[] { "--session-token", "first", "--session-token", "second" },
        new string?[] { "--editor-pid", "1", "--editor-pid", "2" },
        new string?[] { "--smoke-test", "--smoke-test" },
        new string?[] { "--pipe", "partial" },
        new string?[] { "--session-token", new string('a', 32) },
        new string?[] { "--editor-pid", "42" },
        new string?[] { "--smoke-test", "quest.pxsl" },
        new string?[]
        {
            "--smoke-test",
            "--pipe", "dluma.questgraph.synthetic",
            "--session-token", new string('a', 32),
            "--editor-pid", "42"
        }
    };
}
