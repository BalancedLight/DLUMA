using System.Text.RegularExpressions;

namespace Dluma.QuestGraph.Core;

internal sealed record QuestInspectorMacroContract(
    string Name,
    string Section,
    IReadOnlyList<QuestCatalogParameter> Parameters,
    IReadOnlyList<string> Body,
    string? ResolvedPhaseType,
    IReadOnlyList<string> DescriptionLines);

internal sealed record QuestInspectorPhaseContract(
    string Name,
    IReadOnlyList<QuestCatalogParameter> Parameters);

internal static partial class QuestInspectorFieldDeriver
{
    public static IReadOnlyList<QuestInspectorFieldDefinition> Derive(
        QuestInspectorMacroContract macro,
        IReadOnlyDictionary<string, QuestInspectorMacroContract> macros,
        IReadOnlyDictionary<string, QuestInspectorPhaseContract> phases)
    {
        var signature = macro.DescriptionLines.FirstOrDefault(line =>
            line.TrimStart().StartsWith("," + macro.Name, StringComparison.OrdinalIgnoreCase));
        var config = ParseConfigSignature(signature, macro.Parameters);
        var fields = new MutableField[macro.Parameters.Count];
        for (var index = 0; index < macro.Parameters.Count; ++index)
        {
            var parameter = macro.Parameters[index];
            var evidence = InferBase(macro, parameter.Name);
            var expansion = ResolveExpansionEvidence(
                macro,
                parameter.Name,
                macros,
                phases,
                new HashSet<string>(StringComparer.OrdinalIgnoreCase),
                0);
            evidence = Merge(evidence, expansion);
            config.TryGetValue(parameter.Name, out var configured);
            evidence = Merge(evidence, configured?.Evidence);
            evidence = ApplyAmbiguityOverrides(macro, parameter.Name, evidence);
            var role = parameter.Name.Equals("name", StringComparison.OrdinalIgnoreCase) &&
                !macro.Section.Equals("SubPhase", StringComparison.OrdinalIgnoreCase) &&
                !macro.Section.Equals("Forbidden", StringComparison.OrdinalIgnoreCase)
                ? QuestInspectorFieldRole.PhaseName
                : QuestInspectorFieldRole.Parameter;
            if (role == QuestInspectorFieldRole.PhaseName)
                evidence = evidence with
                {
                    EditorKind = QuestParameterEditorKind.Text,
                    TypeName = "string",
                    Priority = 100
                };
            fields[index] = new MutableField(
                "parameter:" + parameter.Name.ToLowerInvariant(),
                parameter.Name,
                DisplayName(parameter.Name, role),
                evidence.EditorKind,
                role,
                evidence.TypeName,
                role == QuestInspectorFieldRole.PhaseName ||
                    (configured?.NamedArgument is null &&
                        expansion?.IsRequired != false &&
                        parameter.IsRequired),
                parameter.Position ?? index,
                configured?.NamedArgument,
                evidence.AllowedValues,
                null,
                DescriptionFor(parameter.Name, macro.DescriptionLines));
        }

        PairObjectFields(fields);
        return fields.Select(field => field.Build()).ToArray();
    }

    public static IReadOnlyList<QuestInspectorFieldDefinition> DeriveQuest(
        IReadOnlyList<string> descriptionLines)
    {
        var signature = descriptionLines.FirstOrDefault(line =>
            line.TrimStart().StartsWith("quest", StringComparison.OrdinalIgnoreCase));
        if (string.IsNullOrWhiteSpace(signature))
            return Array.Empty<QuestInspectorFieldDefinition>();

        var fields = new List<QuestInspectorFieldDefinition>
        {
            new(
                "node:name",
                "quest_name",
                "Quest Name",
                QuestParameterEditorKind.Text,
                QuestInspectorFieldRole.NodeName,
                "string",
                true,
                null,
                null,
                Array.Empty<string>(),
                null,
                "Top-level quest name.")
        };
        foreach (Match match in NamedSignaturePattern().Matches(signature))
        {
            var name = match.Groups["name"].Value;
            var hint = TrimHint(match.Groups["hint"].Value);
            var evidence = InferHint(name, hint);
            fields.Add(new QuestInspectorFieldDefinition(
                "parameter:" + name.ToLowerInvariant(),
                name,
                DisplayName(name, QuestInspectorFieldRole.Parameter),
                evidence.EditorKind,
                QuestInspectorFieldRole.Parameter,
                evidence.TypeName,
                false,
                null,
                name,
                evidence.AllowedValues,
                null,
                DescriptionFor(name, descriptionLines)));
        }
        return fields
            .GroupBy(field => field.Id, StringComparer.OrdinalIgnoreCase)
            .Select(group => group.First())
            .ToArray();
    }

    public static IReadOnlyList<QuestInspectorFieldDefinition> DeriveSupporting(
        IReadOnlyList<QuestCatalogParameter> parameters)
    {
        return parameters.Select((parameter, index) =>
        {
            var evidence = FromType(
                parameter.Type,
                parameter.Values,
                parameter.IsRequired,
                priority: 70);
            return new QuestInspectorFieldDefinition(
                "parameter:" + parameter.Name.ToLowerInvariant(),
                parameter.Name,
                DisplayName(parameter.Name, QuestInspectorFieldRole.Parameter),
                evidence.EditorKind,
                QuestInspectorFieldRole.Parameter,
                evidence.TypeName,
                parameter.IsRequired,
                parameter.Position ?? index,
                null,
                evidence.AllowedValues,
                null,
                string.Empty);
        }).ToArray();
    }

    private static IReadOnlyDictionary<string, ConfigBinding> ParseConfigSignature(
        string? signature,
        IReadOnlyList<QuestCatalogParameter> parameters)
    {
        var result = new Dictionary<string, ConfigBinding>(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(signature))
            return result;

        foreach (Match match in NamedSignaturePattern().Matches(signature))
        {
            var option = match.Groups["name"].Value;
            var parameter = parameters.FirstOrDefault(candidate =>
                candidate.Name.Equals(option, StringComparison.OrdinalIgnoreCase));
            if (parameter is null)
                continue;
            var hint = TrimHint(match.Groups["hint"].Value);
            result[parameter.Name] = new ConfigBinding(option, InferHint(parameter.Name, hint));
        }

        var commandEnd = signature.IndexOf(' ');
        var positionalCursor = 0;
        foreach (Match match in BracketHintPattern().Matches(signature))
        {
            if (match.Index <= commandEnd ||
                match.Index > 0 && signature[match.Index - 1] == '=')
                continue;
            var hint = match.Groups["hint"].Value.Trim();
            var parameter = MatchHintParameter(hint, parameters, ref positionalCursor);
            if (parameter is null || result.ContainsKey(parameter.Name))
                continue;
            result[parameter.Name] = new ConfigBinding(null, InferHint(parameter.Name, hint));
        }
        return result;
    }

    private static QuestCatalogParameter? MatchHintParameter(
        string hint,
        IReadOnlyList<QuestCatalogParameter> parameters,
        ref int positionalCursor)
    {
        var normalized = hint.ToLowerInvariant().Replace('-', '_');
        string? preferred = normalized switch
        {
            "phase_name" => "name",
            "object_class" or "objects_class" => "class",
            "object_name" => parameters.Any(parameter =>
                parameter.Name.Equals("object", StringComparison.OrdinalIgnoreCase))
                ? "object"
                : "scenario_name",
            _ => null
        };
        if (preferred is not null)
        {
            var matched = parameters.FirstOrDefault(parameter =>
                parameter.Name.Equals(preferred, StringComparison.OrdinalIgnoreCase));
            if (matched is not null)
            {
                positionalCursor = Math.Max(positionalCursor, (matched.Position ?? 0) + 1);
                return matched;
            }
        }
        var exact = parameters.FirstOrDefault(parameter =>
            parameter.Name.Equals(normalized, StringComparison.OrdinalIgnoreCase));
        if (exact is not null)
            return exact;
        while (positionalCursor < parameters.Count)
            return parameters[positionalCursor++];
        return null;
    }

    private static FieldEvidence? ResolveExpansionEvidence(
        QuestInspectorMacroContract macro,
        string parameterName,
        IReadOnlyDictionary<string, QuestInspectorMacroContract> macros,
        IReadOnlyDictionary<string, QuestInspectorPhaseContract> phases,
        HashSet<string> visited,
        int depth)
    {
        if (depth >= 32 || !visited.Add(macro.Name + ":" + parameterName))
            return null;
        FieldEvidence? best = null;
        var marker = "<(," + parameterName + ")>";
        foreach (var rawLine in macro.Body)
        {
            var line = rawLine.Trim();
            if (!line.Contains(marker, StringComparison.OrdinalIgnoreCase))
                continue;
            foreach (Match match in AttributeBindingPattern().Matches(line))
            {
                if (!match.Groups["parameter"].Value.Equals(parameterName, StringComparison.OrdinalIgnoreCase))
                    continue;
                var attribute = match.Groups["attribute"].Value;
                best = Merge(best, PhaseAttributeEvidence(attribute, macro.ResolvedPhaseType, phases));
            }

            var invocation = MacroInvocationPattern().Match(line);
            if (!invocation.Success ||
                !macros.TryGetValue(invocation.Groups["name"].Value, out var target))
                continue;
            var tokens = DefinitionTokens(line).Skip(1).ToArray();
            var positional = 0;
            foreach (var token in tokens)
            {
                string? targetParameter = null;
                var equals = token.IndexOf('=');
                if (token.StartsWith('-') && equals > 1)
                    targetParameter = token[1..equals];
                else if (!token.StartsWith('-') &&
                    positional < target.Parameters.Count)
                    targetParameter = target.Parameters[positional++].Name;
                if (targetParameter is null ||
                    !token.Contains(marker, StringComparison.OrdinalIgnoreCase))
                    continue;
                var propagated = ResolveExpansionEvidence(
                    target,
                    targetParameter,
                    macros,
                    phases,
                    visited,
                    depth + 1);
                best = Merge(best, propagated);
                best = Merge(best, InferBase(target, targetParameter) with { Priority = 75 });
            }
        }
        visited.Remove(macro.Name + ":" + parameterName);
        return best;
    }

    private static FieldEvidence PhaseAttributeEvidence(
        string attribute,
        string? phaseName,
        IReadOnlyDictionary<string, QuestInspectorPhaseContract> phases)
    {
        if (!string.IsNullOrWhiteSpace(phaseName) &&
            phases.TryGetValue(phaseName, out var phase))
        {
            var parameter = phase.Parameters.FirstOrDefault(candidate =>
                candidate.Name.Equals(attribute, StringComparison.OrdinalIgnoreCase));
            if (parameter is not null)
                return FromType(parameter.Type, parameter.Values, parameter.IsRequired, 90);
        }
        return InferName(attribute, priority: 60);
    }

    private static FieldEvidence InferBase(
        QuestInspectorMacroContract macro,
        string parameterName)
    {
        if (parameterName.Equals("name", StringComparison.OrdinalIgnoreCase) &&
            !macro.Section.Equals("SubPhase", StringComparison.OrdinalIgnoreCase) &&
            !macro.Section.Equals("Forbidden", StringComparison.OrdinalIgnoreCase))
            return new(QuestParameterEditorKind.Text, "string", Array.Empty<string>(), true, 95);
        return InferName(parameterName, priority: 30);
    }

    private static FieldEvidence ApplyAmbiguityOverrides(
        QuestInspectorMacroContract macro,
        string parameterName,
        FieldEvidence evidence)
    {
        if (macro.Name.Equals("interactive-movie", StringComparison.OrdinalIgnoreCase) &&
            parameterName.Equals("scenario_name", StringComparison.OrdinalIgnoreCase))
            return evidence with
            {
                EditorKind = QuestParameterEditorKind.Scenario,
                TypeName = "scenario",
                Priority = 110
            };
        if (parameterName.Equals("class", StringComparison.OrdinalIgnoreCase) &&
            macro.Parameters.Any(parameter =>
                parameter.Name.Equals("object", StringComparison.OrdinalIgnoreCase) ||
                parameter.Name.Equals("scenario_name", StringComparison.OrdinalIgnoreCase)))
            return evidence with
            {
                EditorKind = QuestParameterEditorKind.MapObjectClass,
                TypeName = "object-class",
                Priority = 105
            };
        if (parameterName.Equals("object", StringComparison.OrdinalIgnoreCase) &&
            macro.Parameters.Any(parameter => parameter.Name.Equals("class", StringComparison.OrdinalIgnoreCase)))
            return evidence with
            {
                EditorKind = QuestParameterEditorKind.MapObjectName,
                TypeName = "object-name",
                Priority = 105
            };
        return evidence;
    }

    private static FieldEvidence InferHint(string parameterName, string hint)
    {
        var normalized = hint.Trim('[', ']').Trim();
        if (normalized.Contains("TEXT_ID", StringComparison.OrdinalIgnoreCase) ||
            normalized.Contains("<<text>>", StringComparison.OrdinalIgnoreCase))
            return new(QuestParameterEditorKind.TextId, "text-id", Array.Empty<string>(), false, 105);
        if (normalized.Contains("literal", StringComparison.OrdinalIgnoreCase))
            return new(QuestParameterEditorKind.Literal, "literal", Array.Empty<string>(), false, 105);
        var values = normalized.Split('|', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (values.Length > 1 && values.All(value => value.Length <= 64 && !value.Contains(' ')))
        {
            if (values.Length == 2 && values.Contains("true", StringComparer.OrdinalIgnoreCase) &&
                values.Contains("false", StringComparer.OrdinalIgnoreCase))
                return new(QuestParameterEditorKind.Boolean, "bool", new[] { "true", "false" }, false, 105);
            return new(QuestParameterEditorKind.Enum, "enum", values, false, 105);
        }
        return InferName(parameterName, priority: 45);
    }

    private static FieldEvidence InferName(string parameterName, int priority)
    {
        var name = parameterName.ToLowerInvariant().Replace('-', '_');
        if (name is "class" or "object_class" or "objects_class")
            return new(QuestParameterEditorKind.MapObjectClass, "object-class", Array.Empty<string>(), true, priority + 20);
        if (name is "object" or "object_name")
            return new(QuestParameterEditorKind.MapObjectName, "object-name", Array.Empty<string>(), true, priority + 15);
        if (name is "scenario" or "scenario_name")
            return new(QuestParameterEditorKind.Scenario, "scenario", Array.Empty<string>(), true, priority + 20);
        if (name.Contains("literal", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.Literal, "literal", Array.Empty<string>(), true, priority + 20);
        if (name.Contains("speaker", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.Speaker, "speaker", Array.Empty<string>(), false, priority + 20);
        if (name is "group" or "dialog_group" or "follow" ||
            name.Contains("dialog_group", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.DialogueGroup, "dialogue-group", Array.Empty<string>(), false, priority + 20);
        if (name is "obj" or "objective" or "text" or "text_id" ||
            name.Contains("description", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.TextId, "text-id", Array.Empty<string>(), false, priority + 18);
        if (name.Contains("quest", StringComparison.Ordinal) &&
            !name.Contains("indicator", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.QuestReference, "quest-reference", Array.Empty<string>(), false, priority + 15);
        if (name.StartsWith("is_", StringComparison.Ordinal) ||
            name.StartsWith("has_", StringComparison.Ordinal) ||
            name.StartsWith("show_", StringComparison.Ordinal) ||
            name.StartsWith("allow_", StringComparison.Ordinal) ||
            name is "enc" or "auto" or "show" or "outside" or "force" or "block")
            return new(QuestParameterEditorKind.Boolean, "bool", new[] { "true", "false" }, false, priority + 10);
        if (name.Contains("count", StringComparison.Ordinal) ||
            name.Contains("distance", StringComparison.Ordinal) ||
            name.Contains("radius", StringComparison.Ordinal) ||
            name.Contains("time", StringComparison.Ordinal) ||
            name.Contains("amount", StringComparison.Ordinal) ||
            name.EndsWith("_level", StringComparison.Ordinal))
            return new(QuestParameterEditorKind.Number, "number", Array.Empty<string>(), false, priority + 8);
        return new(QuestParameterEditorKind.Text, "string", Array.Empty<string>(), true, priority);
    }

    private static FieldEvidence FromType(
        string type,
        IReadOnlyList<string> values,
        bool isRequired,
        int priority)
    {
        var normalized = type.Trim().ToLowerInvariant();
        return normalized switch
        {
            "bool" or "boolean" => new(
                QuestParameterEditorKind.Boolean, "bool", new[] { "true", "false" }, isRequired, priority),
            "int" or "integer" or "float" or "double" or "number" => new(
                QuestParameterEditorKind.Number, normalized, Array.Empty<string>(), isRequired, priority),
            "enum" => new(
                QuestParameterEditorKind.Enum, "enum", values, isRequired, priority),
            _ => new(QuestParameterEditorKind.Text, normalized, values, isRequired, priority)
        };
    }

    private static FieldEvidence Merge(FieldEvidence? current, FieldEvidence? candidate)
    {
        if (current is null)
            return candidate ?? new FieldEvidence(
                QuestParameterEditorKind.Text, "string", Array.Empty<string>(), true, 0);
        if (candidate is null)
            return current;
        if (candidate.Priority < current.Priority)
            return current;
        var values = candidate.AllowedValues.Count > 0
            ? candidate.AllowedValues
            : current.AllowedValues;
        return candidate with { AllowedValues = values };
    }

    private static void PairObjectFields(MutableField[] fields)
    {
        foreach (var classField in fields.Where(field =>
            field.EditorKind == QuestParameterEditorKind.MapObjectClass))
        {
            var nameField = fields.FirstOrDefault(field =>
                field.Position > classField.Position &&
                field.EditorKind is QuestParameterEditorKind.MapObjectName or QuestParameterEditorKind.Scenario);
            if (nameField is null)
                continue;
            classField.PairedFieldId = nameField.Id;
            nameField.PairedFieldId = classField.Id;
        }
    }

    private static string DisplayName(string name, QuestInspectorFieldRole role)
    {
        if (role == QuestInspectorFieldRole.PhaseName)
            return "Phase Name";
        return string.Join(' ', name.Split(new[] { '_', '-' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(part => char.ToUpperInvariant(part[0]) + part[1..]));
    }

    private static string DescriptionFor(string parameterName, IReadOnlyList<string> lines) =>
        lines.FirstOrDefault(line => line.Contains(parameterName, StringComparison.OrdinalIgnoreCase)) ?? string.Empty;

    private static string TrimHint(string value) => value.Trim().Trim('[', ']');

    private static IEnumerable<string> DefinitionTokens(string line)
    {
        foreach (Match match in DefinitionTokenPattern().Matches(line))
            yield return match.Value;
    }

    [GeneratedRegex("-(?<name>[A-Za-z0-9_.-]+)(?:=(?<hint>\\[[^\\]]*\\]|<<.*?>>|[^\\s]*))?", RegexOptions.CultureInvariant)]
    private static partial Regex NamedSignaturePattern();

    [GeneratedRegex("\\[(?<hint>[^\\]]+)\\]", RegexOptions.CultureInvariant)]
    private static partial Regex BracketHintPattern();

    [GeneratedRegex("-(?<attribute>[A-Za-z0-9_.-]+)=<\\(,(?<parameter>[A-Za-z0-9_.-]+)\\)>", RegexOptions.CultureInvariant)]
    private static partial Regex AttributeBindingPattern();

    [GeneratedRegex("^,(?<name>[A-Za-z0-9_.-]+)(?:\\s|$)", RegexOptions.CultureInvariant)]
    private static partial Regex MacroInvocationPattern();

    [GeneratedRegex("<<.*?>>|<\\(,.*?\\)>|\\S+", RegexOptions.CultureInvariant)]
    private static partial Regex DefinitionTokenPattern();

    private sealed record ConfigBinding(string? NamedArgument, FieldEvidence Evidence);

    private sealed record FieldEvidence(
        QuestParameterEditorKind EditorKind,
        string TypeName,
        IReadOnlyList<string> AllowedValues,
        bool IsRequired,
        int Priority);

    private sealed class MutableField(
        string id,
        string parameterName,
        string displayName,
        QuestParameterEditorKind editorKind,
        QuestInspectorFieldRole role,
        string typeName,
        bool isRequired,
        int? position,
        string? namedArgument,
        IReadOnlyList<string> allowedValues,
        string? pairedFieldId,
        string description)
    {
        public string Id { get; } = id;
        public string ParameterName { get; } = parameterName;
        public string DisplayName { get; } = displayName;
        public QuestParameterEditorKind EditorKind { get; } = editorKind;
        public QuestInspectorFieldRole Role { get; } = role;
        public string TypeName { get; } = typeName;
        public bool IsRequired { get; } = isRequired;
        public int? Position { get; } = position;
        public string? NamedArgument { get; } = namedArgument;
        public IReadOnlyList<string> AllowedValues { get; } = allowedValues;
        public string? PairedFieldId { get; set; } = pairedFieldId;
        public string Description { get; } = description;

        public QuestInspectorFieldDefinition Build() => new(
            Id,
            ParameterName,
            DisplayName,
            EditorKind,
            Role,
            TypeName,
            IsRequired,
            Position,
            NamedArgument,
            AllowedValues,
            PairedFieldId,
            Description);
    }
}
