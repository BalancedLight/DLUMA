using System.Text;

namespace Dluma.QuestGraph.Core;

internal static class PxslSourceTokenizer
{
    public static IReadOnlyList<LexicalToken> Tokenize(
        string text,
        PxslTextSpan lineSpan,
        Encoding encoding)
    {
        var tokens = new List<LexicalToken>();
        var tokenStart = -1;
        var quoted = false;
        var angleDepth = 0;
        for (var index = 0; index < text.Length; ++index)
        {
            var character = text[index];
            if (tokenStart < 0)
            {
                if (char.IsWhiteSpace(character))
                    continue;
                tokenStart = index;
            }

            if (character == '"' && angleDepth == 0)
            {
                quoted = !quoted;
                continue;
            }
            if (!quoted && index + 1 < text.Length &&
                character == '<' && text[index + 1] == '<')
            {
                angleDepth++;
                index++;
                continue;
            }
            if (!quoted && angleDepth > 0 && index + 1 < text.Length &&
                character == '>' && text[index + 1] == '>')
            {
                angleDepth--;
                index++;
                continue;
            }
            if (!quoted && angleDepth == 0 && char.IsWhiteSpace(character))
            {
                AddToken(tokens, text, tokenStart, index - tokenStart, lineSpan, encoding);
                tokenStart = -1;
            }
        }
        if (tokenStart >= 0)
            AddToken(tokens, text, tokenStart, text.Length - tokenStart, lineSpan, encoding);
        return tokens;
    }

    public static IReadOnlyList<PxslArgumentToken> Arguments(
        IReadOnlyList<LexicalToken> tokens,
        int headerTokenCount,
        Encoding encoding)
    {
        if (tokens.Count <= headerTokenCount)
            return Array.Empty<PxslArgumentToken>();
        var arguments = new List<PxslArgumentToken>(tokens.Count - headerTokenCount);
        var positionalIndex = 0;
        for (var index = headerTokenCount; index < tokens.Count; ++index)
        {
            var token = tokens[index];
            var rawValue = token.Text;
            string? namedArgument = null;
            var isNamed = rawValue.StartsWith('-') && rawValue.Length > 1;
            var valueCharacterOffset = 0;
            if (isNamed)
            {
                var equals = rawValue.IndexOf('=');
                if (equals >= 0)
                {
                    namedArgument = rawValue[1..equals];
                    valueCharacterOffset = equals + 1;
                    rawValue = rawValue[valueCharacterOffset..];
                }
                else
                {
                    namedArgument = rawValue[1..];
                    valueCharacterOffset = rawValue.Length;
                    rawValue = string.Empty;
                }
            }

            var (value, wrapperPrefixLength, wrapperSuffixLength) = Unwrap(rawValue);
            var fullValueCharacterOffset = checked(valueCharacterOffset + wrapperPrefixLength);
            var valueCharacterLength = checked(rawValue.Length - wrapperPrefixLength - wrapperSuffixLength);
            var valueSpan = Subspan(
                token,
                fullValueCharacterOffset,
                valueCharacterLength,
                encoding);
            arguments.Add(new PxslArgumentToken(
                token.Text,
                value,
                namedArgument,
                isNamed ? null : positionalIndex++,
                token.Span,
                valueSpan,
                isNamed,
                rawValue.Equals("_", StringComparison.Ordinal),
                wrapperPrefixLength > 0));
        }
        return arguments;
    }

    private static (string Value, int PrefixLength, int SuffixLength) Unwrap(string raw)
    {
        var length = PxslValueWrapping.PrefixLength(PxslValueWrapping.Detect(raw));
        return length == 0
            ? (raw, 0, 0)
            : (raw[length..^length], length, length);
    }

    private static PxslTextSpan Subspan(
        LexicalToken token,
        int characterOffset,
        int characterLength,
        Encoding encoding)
    {
        var byteOffset = encoding.GetByteCount(token.Text.AsSpan(0, characterOffset));
        var byteLength = encoding.GetByteCount(token.Text.AsSpan(characterOffset, characterLength));
        return new PxslTextSpan(
            checked(token.Span.Start + byteOffset),
            byteLength,
            token.Span.Line,
            checked(token.Span.Column + characterOffset));
    }

    private static void AddToken(
        List<LexicalToken> tokens,
        string text,
        int characterStart,
        int characterLength,
        PxslTextSpan lineSpan,
        Encoding encoding)
    {
        var byteStart = encoding.GetByteCount(text.AsSpan(0, characterStart));
        var tokenText = text.Substring(characterStart, characterLength);
        var byteLength = encoding.GetByteCount(tokenText);
        tokens.Add(new LexicalToken(
            tokenText,
            characterStart,
            characterLength,
            new PxslTextSpan(
                checked(lineSpan.Start + byteStart),
                byteLength,
                lineSpan.Line,
                checked(lineSpan.Column + characterStart))));
    }

    internal sealed record LexicalToken(
        string Text,
        int CharacterStart,
        int CharacterLength,
        PxslTextSpan Span);
}
