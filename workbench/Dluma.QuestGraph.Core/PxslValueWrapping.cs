namespace Dluma.QuestGraph.Core;

/// <summary>The three ways PXSL delimits an argument value.</summary>
internal enum PxslValueWrapper
{
    None,

    /// <summary>A <c>&lt;&lt;label&gt;&gt;</c> phase name or free-text label.</summary>
    AngleBrackets,

    /// <summary>A <c>"quoted"</c> literal.</summary>
    Quotes,

    /// <summary>An <c>&amp;TEXT_ID&amp;</c> localisation reference.</summary>
    Ampersands
}

/// <summary>
/// The single definition of how a PXSL argument value is delimited. The tokenizer and the
/// inspector's write path both need it, and they must agree: the writer strips only the
/// wrapper the slot it is writing into actually uses, so a quote inside a
/// <c>&lt;&lt;…&gt;&gt;</c> label stays content instead of being mistaken for a wrapper.
/// </summary>
internal static class PxslValueWrapping
{
    public static PxslValueWrapper Detect(string raw)
    {
        if (raw.Length >= 4 && raw.StartsWith("<<", StringComparison.Ordinal) &&
            raw.EndsWith(">>", StringComparison.Ordinal))
            return PxslValueWrapper.AngleBrackets;
        if (raw.Length >= 2 && raw.StartsWith('"') && raw.EndsWith('"'))
            return PxslValueWrapper.Quotes;
        if (raw.Length >= 2 && raw.StartsWith('&') && raw.EndsWith('&'))
            return PxslValueWrapper.Ampersands;
        return PxslValueWrapper.None;
    }

    public static int PrefixLength(PxslValueWrapper wrapper) => wrapper switch
    {
        PxslValueWrapper.AngleBrackets => 2,
        PxslValueWrapper.Quotes or PxslValueWrapper.Ampersands => 1,
        _ => 0
    };

    /// <summary>Removes any one wrapper layer, whichever kind it is.</summary>
    public static string Strip(string raw)
    {
        var length = PrefixLength(Detect(raw));
        return length == 0 ? raw : raw[length..^length];
    }

    /// <summary>
    /// Removes <paramref name="wrapper"/> from <paramref name="raw"/> and nothing else.
    /// Use this when writing into a slot that already carries that wrapper.
    /// </summary>
    public static string Strip(string raw, PxslValueWrapper wrapper)
    {
        if (wrapper == PxslValueWrapper.None || Detect(raw) != wrapper)
            return raw;
        var length = PrefixLength(wrapper);
        return raw[length..^length];
    }

    public static string Wrap(string value, PxslValueWrapper wrapper) => wrapper switch
    {
        PxslValueWrapper.AngleBrackets => "<<" + value + ">>",
        PxslValueWrapper.Quotes => '"' + value + '"',
        PxslValueWrapper.Ampersands => '&' + value + '&',
        _ => value
    };

    /// <summary>
    /// The value portion of a raw argument token, dropping a leading <c>-name=</c>.
    /// </summary>
    public static string RawValue(PxslArgumentToken token)
    {
        if (!token.IsNamed)
            return token.RawText;
        var equals = token.RawText.IndexOf('=');
        return equals >= 0 ? token.RawText[(equals + 1)..] : string.Empty;
    }
}
