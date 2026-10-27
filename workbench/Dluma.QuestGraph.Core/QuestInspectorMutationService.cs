using System.Globalization;
using System.Text;

namespace Dluma.QuestGraph.Core;

public static class QuestInspectorMutationService
{
    public static QuestGraphMutation SetFieldValue(
        QuestGraphDocument document,
        string semanticNodeId,
        string fieldId,
        string? value)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentException.ThrowIfNullOrWhiteSpace(semanticNodeId);
        ArgumentException.ThrowIfNullOrWhiteSpace(fieldId);
        var node = document.SemanticNodes.FirstOrDefault(candidate =>
            candidate.Id.Equals(semanticNodeId, StringComparison.Ordinal)) ??
            throw new QuestInspectorException(
                QuestInspectorRejection.NodeNotFound,
                "The selected graph node no longer exists in the PXSL source.");
        if (node.IsOpaque)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.OpaqueNode,
                "Opaque PXSL must be repaired in the source editor or deleted by its exact span.");
        }
        var entryKey = node.Kind == QuestSemanticKind.Quest ? "quest" : node.Name;
        if (document.Catalog?.TryGetEntry(entryKey, out var entry) != true || entry is null)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.CatalogUnavailable,
                "The selected graph node has no active definition-driven inspector contract.");
        }
        var field = entry.InspectorFields.FirstOrDefault(candidate =>
            candidate.Id.Equals(fieldId, StringComparison.OrdinalIgnoreCase)) ??
            throw new QuestInspectorException(
                QuestInspectorRejection.FieldNotFound,
                $"Inspector field '{fieldId}' is absent from the active catalog entry.");
        var normalized = NormalizeAndValidate(field, value);
        var encoding = QuestEncoding.Resolve(document.Source.EncodingName);

        if (field.Role == QuestInspectorFieldRole.NodeName)
            return ReplaceNodeName(document, node, field, normalized, encoding);

        var token = FindToken(node, field);
        if (token is not null)
            return ReplaceToken(document, node, field, token, normalized, encoding);
        return InsertMissing(document, node, field, normalized, encoding);
    }

    private static QuestGraphMutation ReplaceNodeName(
        QuestGraphDocument document,
        QuestSemanticNode node,
        QuestInspectorFieldDefinition field,
        string value,
        Encoding encoding)
    {
        var span = node.NameSpan ?? throw new QuestInspectorException(
            QuestInspectorRejection.InvalidSourceRange,
            "The selected node has no lossless name span.");
        var raw = Decode(document.Source, span);
        var replacement = FormatWholeValue(field, value, raw, preserveExistingWrapper: true);
        return Mutation(document, span.Start, span.Length, Encode(encoding, replacement),
            $"Updated {field.DisplayName}.");
    }

    private static QuestGraphMutation ReplaceToken(
        QuestGraphDocument document,
        QuestSemanticNode node,
        QuestInspectorFieldDefinition field,
        PxslArgumentToken token,
        string value,
        Encoding encoding)
    {
        if (value.Length == 0 && field.Role != QuestInspectorFieldRole.PhaseName)
        {
            if (token.IsNamed && !field.IsRequired)
            {
                return Mutation(document, token.Span.Start, token.Span.Length,
                    Array.Empty<byte>(), $"Cleared {field.DisplayName}.");
            }
            value = "_";
        }

        if (token.IsNamed && !token.RawText.Contains('='))
        {
            var whole = "-" + (field.NamedArgument ?? token.NamedArgument) + "=" +
                FormatWholeValue(field, value, string.Empty, preserveExistingWrapper: false);
            return Mutation(document, token.Span.Start, token.Span.Length,
                Encode(encoding, whole), $"Updated {field.DisplayName}.");
        }

        if (value.Equals("_", StringComparison.Ordinal) && token.IsWrapped)
        {
            var placeholder = token.IsNamed
                ? "-" + (field.NamedArgument ?? token.NamedArgument) + "=_"
                : "_";
            return Mutation(document, token.Span.Start, token.Span.Length,
                Encode(encoding, placeholder), $"Updated {field.DisplayName}.");
        }

        // ValueSpan is the text inside the slot's existing wrapper, so strip only that
        // same wrapper from the typed value. Anything else -- a quote inside a << >>
        // phase name, say -- is content and must survive.
        var replacement = token.IsWrapped
            ? PxslValueWrapping.Strip(
                value,
                PxslValueWrapping.Detect(PxslValueWrapping.RawValue(token)))
            : FormatWholeValue(field, value, token.RawText, preserveExistingWrapper: false);
        return Mutation(document, token.ValueSpan.Start, token.ValueSpan.Length,
            Encode(encoding, replacement), $"Updated {field.DisplayName}.");
    }

    private static QuestGraphMutation InsertMissing(
        QuestGraphDocument document,
        QuestSemanticNode node,
        QuestInspectorFieldDefinition field,
        string value,
        Encoding encoding)
    {
        if (value.Length == 0 && field.Role != QuestInspectorFieldRole.PhaseName)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.InvalidValue,
                $"Inspector field '{field.DisplayName}' is already absent.");
        }
        if (field.Role == QuestInspectorFieldRole.PhaseName && value.Length == 0)
            value = "_";
        var formatted = FormatWholeValue(field, value, string.Empty, preserveExistingWrapper: false);
        if (!string.IsNullOrWhiteSpace(field.NamedArgument))
        {
            var insertion = " -" + field.NamedArgument + "=" + formatted;
            return Mutation(document, node.Span.End, 0, Encode(encoding, insertion),
                $"Set {field.DisplayName}.");
        }
        if (field.Position is not int position)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.InvalidSourceRange,
                $"Inspector field '{field.DisplayName}' has no representable PXSL binding.");
        }

        var positionalCount = node.ArgumentTokens.Count(token => !token.IsNamed);
        if (positionalCount > position)
            RejectRange("The positional inspector binding disagrees with the parsed argument spans.");
        var insertedValues = Enumerable.Repeat("_", Math.Max(0, position - positionalCount))
            .Append(formatted)
            .ToArray();
        var firstNamed = node.ArgumentTokens.FirstOrDefault(token => token.IsNamed);
        var offset = firstNamed?.Span.Start ?? node.Span.End;
        var insertionText = firstNamed is null
            ? " " + string.Join(' ', insertedValues)
            : string.Join(' ', insertedValues) + " ";
        return Mutation(document, offset, 0, Encode(encoding, insertionText),
            $"Set {field.DisplayName}.");
    }

    private static PxslArgumentToken? FindToken(
        QuestSemanticNode node,
        QuestInspectorFieldDefinition field)
    {
        if (!string.IsNullOrWhiteSpace(field.NamedArgument))
        {
            var named = node.ArgumentTokens.FirstOrDefault(token =>
                token.IsNamed && token.NamedArgument?.Equals(
                    field.NamedArgument,
                    StringComparison.OrdinalIgnoreCase) == true);
            if (named is not null)
                return named;
        }
        return field.Position is int position
            ? node.ArgumentTokens.FirstOrDefault(token =>
                !token.IsNamed && token.Position == position)
            : null;
    }

    private static string NormalizeAndValidate(
        QuestInspectorFieldDefinition field,
        string? value)
    {
        var normalized = value?.Trim() ?? string.Empty;
        if (normalized.Length == 0 &&
            field.Role is QuestInspectorFieldRole.PhaseName or QuestInspectorFieldRole.NodeName)
            return "_";
        if (normalized.Length == 0)
            return string.Empty;
        var plain = UnwrapInput(normalized);
        switch (field.EditorKind)
        {
            case QuestParameterEditorKind.Boolean:
                if (plain.Equals("1", StringComparison.Ordinal))
                    return "true";
                if (plain.Equals("0", StringComparison.Ordinal))
                    return "false";
                if (!bool.TryParse(plain, out var boolean))
                {
                    throw new QuestInspectorException(
                        QuestInspectorRejection.InvalidValue,
                        $"'{value}' is not a true/false value for {field.DisplayName}.");
                }
                return boolean ? "true" : "false";
            case QuestParameterEditorKind.Number:
                if (!double.TryParse(plain, NumberStyles.Float,
                    CultureInfo.InvariantCulture, out _))
                {
                    throw new QuestInspectorException(
                        QuestInspectorRejection.InvalidValue,
                        $"'{value}' is not an invariant number for {field.DisplayName}.");
                }
                return plain;
            case QuestParameterEditorKind.Enum when field.AllowedValues.Count > 0:
                var allowed = field.AllowedValues.FirstOrDefault(candidate =>
                    candidate.Equals(plain, StringComparison.OrdinalIgnoreCase));
                if (allowed is null)
                {
                    throw new QuestInspectorException(
                        QuestInspectorRejection.InvalidValue,
                        $"'{value}' is not an allowed value for {field.DisplayName}.");
                }
                return allowed;
            default:
                return normalized;
        }
    }

    private static string FormatWholeValue(
        QuestInspectorFieldDefinition field,
        string value,
        string existingRaw,
        bool preserveExistingWrapper)
    {
        if (value.Equals("_", StringComparison.Ordinal))
            return "_";

        // A phase name has exactly two legal forms: _ when blank, otherwise <<name>>.
        // This runs before the explicit-wrapper check below, which would otherwise let a
        // quoted name through bare -- and quotes are content here, not a wrapper.
        if (field.Role == QuestInspectorFieldRole.PhaseName)
        {
            return PxslValueWrapping.Detect(value) == PxslValueWrapper.AngleBrackets
                ? value
                : PxslValueWrapping.Wrap(value, PxslValueWrapper.AngleBrackets);
        }

        if (IsExplicitlyWrapped(value))
            return value;
        var plain = UnwrapInput(value);
        if (preserveExistingWrapper)
        {
            if (existingRaw.StartsWith("<<", StringComparison.Ordinal) &&
                existingRaw.EndsWith(">>", StringComparison.Ordinal))
                return "<<" + plain + ">>";
            if (existingRaw.StartsWith('"') && existingRaw.EndsWith('"'))
                return '"' + plain + '"';
            if (existingRaw.StartsWith('&') && existingRaw.EndsWith('&'))
                return '&' + plain + '&';
        }
        if (field.EditorKind == QuestParameterEditorKind.TextId)
            return plain.Any(char.IsWhiteSpace) ? "<<" + plain + ">>" : '&' + plain + '&';
        return plain.Any(char.IsWhiteSpace) ? "<<" + plain + ">>" : plain;
    }

    private static bool IsExplicitlyWrapped(string value) =>
        PxslValueWrapping.Detect(value) != PxslValueWrapper.None;

    private static string UnwrapInput(string value) => PxslValueWrapping.Strip(value);

    private static QuestGraphMutation Mutation(
        QuestGraphDocument document,
        int offset,
        int removedLength,
        ReadOnlyMemory<byte> replacement,
        string description)
    {
        if (offset < 0 || removedLength < 0 || offset > document.Source.Bytes.Length - removedLength)
            RejectRange("The inspector edit is outside the current source snapshot.");
        return new QuestGraphMutation(
            new[]
            {
                new QuestTextEdit(
                    document.Source.Revision,
                    document.Source.Sha256,
                    offset,
                    removedLength,
                    replacement)
            },
            description);
    }

    private static byte[] Encode(Encoding encoding, string value)
    {
        try
        {
            return encoding.GetBytes(value);
        }
        catch (EncoderFallbackException exception)
        {
            throw new QuestInspectorException(
                QuestInspectorRejection.UnrepresentableText,
                $"The inspector value cannot be represented as {encoding.WebName}: {exception.Message}");
        }
    }

    private static string Decode(QuestSourceSnapshot source, PxslTextSpan span)
    {
        if (span.Start < 0 || span.Length < 0 || span.End > source.Bytes.Length)
            RejectRange("The selected inspector value is outside the current source snapshot.");
        return QuestEncoding.Resolve(source.EncodingName).GetString(
            source.Bytes.Span.Slice(span.Start, span.Length));
    }

    [System.Diagnostics.CodeAnalysis.DoesNotReturn]
    private static void RejectRange(string message) =>
        throw new QuestInspectorException(QuestInspectorRejection.InvalidSourceRange, message);
}
