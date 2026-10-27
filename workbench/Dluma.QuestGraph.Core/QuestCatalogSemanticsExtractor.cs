using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace Dluma.QuestGraph.Core;

/// <summary>
/// Builds the authoring catalog from the same declarative inputs consumed by the
/// stock quest editor. Definition order is significant: a project-private input
/// supplied before an installed input wins, while the other input kinds can still
/// contribute descriptions, styles, and structural metadata.
/// </summary>
internal static partial class QuestCatalogSemanticsExtractor
{
    private const int MaximumInputBytes = 32 * 1024 * 1024;

    public static QuestCatalog Extract(IEnumerable<QuestCatalogInput> inputs)
    {
        ArgumentNullException.ThrowIfNull(inputs);
        var state = new ExtractionState();
        var provenance = new List<QuestCatalogProvenance>();

        foreach (var input in inputs)
        {
            ArgumentNullException.ThrowIfNull(input);
            ArgumentException.ThrowIfNullOrWhiteSpace(input.Name);
            ArgumentNullException.ThrowIfNull(input.Content);

            var bytes = ReadBounded(input.Content, MaximumInputBytes);
            var format = ResolveFormat(input.Name, input.Format, bytes);
            var source = Path.GetFileName(input.Name);
            if (string.IsNullOrWhiteSpace(source))
                source = "quest-catalog";
            provenance.Add(new QuestCatalogProvenance(source, format, QuestHash.Sha256(bytes)));

            switch (format)
            {
                case QuestCatalogFormat.Edf:
                    ParseEdf(DecodeText(bytes), source, state);
                    break;
                case QuestCatalogFormat.QuestEditorConfig:
                    ParseConfig(DecodeText(bytes), source, state);
                    break;
                case QuestCatalogFormat.PhaseDefinitionsXml:
                    ParsePhaseDefinitions(bytes, source, state);
                    break;
                case QuestCatalogFormat.QuestObjectDefinitionsXml:
                    ParseObjectDefinitions(bytes, source, state);
                    break;
                case QuestCatalogFormat.QuestColorsXml:
                    ParseColors(bytes, source, state);
                    break;
                default:
                    throw new InvalidDataException($"Unsupported quest catalog format: {format}.");
            }
        }

        var entries = Materialize(state);
        var orderedProvenance = provenance
            .OrderBy(item => item.Name, StringComparer.OrdinalIgnoreCase)
            .ThenBy(item => item.Format)
            .ToArray();
        return new QuestCatalog(entries, orderedProvenance, CatalogHash(entries, orderedProvenance));
    }

    private static IReadOnlyList<QuestCatalogEntry> Materialize(ExtractionState state)
    {
        var entries = new List<QuestCatalogEntry>();
        var resolvedPhaseTypes = state.Macros.Values.ToDictionary(
            definition => definition.Name,
            definition => ResolvePhaseType(definition, state.Macros),
            StringComparer.OrdinalIgnoreCase);
        var inspectorMacros = state.Macros.Values.ToDictionary(
            definition => definition.Name,
            definition => new QuestInspectorMacroContract(
                definition.Name,
                definition.Section,
                definition.Parameters,
                definition.Body,
                resolvedPhaseTypes[definition.Name],
                state.Descriptions.GetValueOrDefault(definition.Name)?.Lines ??
                    Array.Empty<string>()),
            StringComparer.OrdinalIgnoreCase);
        var inspectorPhases = state.Phases.Values.ToDictionary(
            phase => phase.Name,
            phase => new QuestInspectorPhaseContract(
                phase.Name,
                phase.Parameters),
            StringComparer.OrdinalIgnoreCase);
        var producedObjectMacros = state.Macros.Values
            .Where(definition => !string.IsNullOrWhiteSpace(definition.ProducedObjectType))
            .GroupBy(definition => definition.ProducedObjectType!, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(
                group => group.Key,
                group => group.Select(item => item.Name)
                    .Distinct(StringComparer.OrdinalIgnoreCase)
                    .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
                    .ToArray(),
                StringComparer.OrdinalIgnoreCase);

        foreach (var definition in state.Macros.Values)
        {
            state.Descriptions.TryGetValue(definition.Name, out var description);
            var resolvedPhaseType = resolvedPhaseTypes[definition.Name];
            state.Phases.TryGetValue(resolvedPhaseType ?? string.Empty, out var phase);
            var childSlots = BuildChildSlots(
                definition,
                description,
                phase,
                producedObjectMacros,
                state.AutoExpand);
            var category = Category(definition, resolvedPhaseType, childSlots);
            var style = ResolveStyle(definition.Name, resolvedPhaseType, state.Styles);
            var template = ResolveTemplate(definition, description);
            var sources = Sources(
                definition.Source,
                description?.Source,
                phase?.Source,
                style?.Source);
            var aliases = state.Macros.Values
                .Where(candidate => string.Equals(
                    candidate.AliasTarget,
                    definition.Name,
                    StringComparison.OrdinalIgnoreCase))
                .Select(candidate => candidate.Name)
                .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
                .ToArray();
            var inspectorFields = QuestInspectorFieldDeriver.Derive(
                inspectorMacros[definition.Name],
                inspectorMacros,
                inspectorPhases);

            entries.Add(new QuestCatalogEntry(
                definition.Name,
                QuestCatalogEntryKind.Macro,
                definition.Parameters,
                childSlots.Select(slot => new QuestCatalogChild(
                    slot.DisplayName,
                    slot.Kind.ToString(),
                    slot.Quantity)).ToArray(),
                sources)
            {
                IsInsertable = !definition.Section.Equals("Forbidden", StringComparison.OrdinalIgnoreCase),
                IsCompileEligible = !definition.Section.Equals("Forbidden", StringComparison.OrdinalIgnoreCase),
                Category = category,
                DefinitionSection = definition.Section,
                ResolvedPhaseType = resolvedPhaseType,
                AliasTarget = definition.AliasTarget,
                Style = style?.Style,
                DescriptionLines = description?.Lines ?? Array.Empty<string>(),
                Template = template,
                DirectiveTemplate = template,
                Aliases = aliases,
                ChildSlots = childSlots,
                InspectorFields = inspectorFields
            });
        }

        if (state.Descriptions.TryGetValue("quest", out var questDescription) &&
            !state.Macros.ContainsKey("quest"))
        {
            var template = questDescription.Lines.FirstOrDefault(line =>
                line.TrimStart().StartsWith("quest ", StringComparison.OrdinalIgnoreCase)) ??
                "quest [quest_name]";
            entries.Add(new QuestCatalogEntry(
                "quest",
                QuestCatalogEntryKind.Macro,
                Array.Empty<QuestCatalogParameter>(),
                Array.Empty<QuestCatalogChild>(),
                Sources(questDescription.Source))
            {
                IsInsertable = true,
                IsCompileEligible = true,
                Category = QuestCatalogFunctionalCategory.FlowAndContainers,
                DefinitionSection = "Quest",
                DescriptionLines = questDescription.Lines,
                Template = template,
                DirectiveTemplate = template,
                ChildSlots =
                [
                    new QuestCatalogChildSlot(
                        "body",
                        "Quest Body",
                        QuestCatalogChildSlotKind.OrderedBody,
                        "*",
                        string.Empty,
                        Array.Empty<string>())
                ],
                InspectorFields = QuestInspectorFieldDeriver.DeriveQuest(questDescription.Lines)
            });
        }

        foreach (var phase in state.Phases.Values)
        {
            state.Styles.TryGetValue(phase.Name, out var style);
            entries.Add(new QuestCatalogEntry(
                phase.Name,
                QuestCatalogEntryKind.Phase,
                phase.Parameters,
                phase.Children.Select(child => new QuestCatalogChild(
                    child.Name,
                    child.Kind,
                    child.Quantity)).ToArray(),
                Sources(phase.Source, style?.Source))
            {
                IsInsertable = false,
                IsCompileEligible = false,
                Category = QuestCatalogFunctionalCategory.SupportingDefinition,
                DefinitionSection = "PhaseTypeDefinitions",
                ResolvedPhaseType = phase.Name,
                Style = style?.Style,
                Template = string.Empty,
                DirectiveTemplate = string.Empty,
                InspectorFields = QuestInspectorFieldDeriver.DeriveSupporting(phase.Parameters)
            });
        }

        foreach (var definition in state.Objects.Values)
        {
            entries.Add(new QuestCatalogEntry(
                definition.Name,
                QuestCatalogEntryKind.QuestObject,
                Array.Empty<QuestCatalogParameter>(),
                definition.Classes.Select(name => new QuestCatalogChild(name, "class", "1")).ToArray(),
                Sources(definition.Source))
            {
                IsInsertable = false,
                IsCompileEligible = false,
                Category = QuestCatalogFunctionalCategory.SupportingDefinition,
                DefinitionSection = "QuestObjectDefinitions",
                Template = string.Empty,
                DirectiveTemplate = string.Empty
            });
        }

        foreach (var branch in state.AutoExpand.Values.SelectMany(value => value)
            .Distinct(StringComparer.OrdinalIgnoreCase))
        {
            entries.Add(new QuestCatalogEntry(
                branch,
                QuestCatalogEntryKind.Branch,
                Array.Empty<QuestCatalogParameter>(),
                Array.Empty<QuestCatalogChild>(),
                Sources(state.AutoExpandSources.GetValueOrDefault(branch)))
            {
                IsInsertable = false,
                IsCompileEligible = false,
                Category = QuestCatalogFunctionalCategory.SupportingDefinition,
                DefinitionSection = "AutoExpand",
                Template = branch,
                DirectiveTemplate = branch
            });
        }

        return entries
            .GroupBy(entry => $"{entry.Kind}:{entry.Name}", StringComparer.OrdinalIgnoreCase)
            .Select(group => group.First())
            .OrderByDescending(entry => entry.IsInsertable)
            .ThenBy(entry => entry.Category)
            .ThenBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static IReadOnlyList<QuestCatalogChildSlot> BuildChildSlots(
        EdfDefinition definition,
        DescriptionDefinition? description,
        PhaseDefinition? phase,
        IReadOnlyDictionary<string, string[]> producedObjectMacros,
        IReadOnlyDictionary<string, IReadOnlyList<string>> autoExpand)
    {
        var slots = new Dictionary<string, QuestCatalogChildSlot>(StringComparer.OrdinalIgnoreCase);

        if (description is not null)
        {
            foreach (var line in description.Lines)
            {
                var trimmed = line.TrimStart();
                if (!trimmed.StartsWith(",", StringComparison.Ordinal))
                    continue;
                var match = ChildMacroPattern().Match(trimmed);
                if (!match.Success)
                    continue;
                var childName = match.Groups["name"].Value;
                if (childName.Equals(definition.Name, StringComparison.OrdinalIgnoreCase))
                    continue;
                var quantity = line.Contains("multiple", StringComparison.OrdinalIgnoreCase) ? "*" : "?";
                slots.TryAdd(childName, new QuestCatalogChildSlot(
                    childName,
                    childName,
                    QuestCatalogChildSlotKind.Child,
                    quantity,
                    trimmed,
                    new[] { childName }));
            }
        }

        if (phase is not null)
        {
            foreach (var child in phase.Children.Where(child =>
                child.Kind.Equals("QuestObject", StringComparison.OrdinalIgnoreCase)))
            {
                var accepted = producedObjectMacros.GetValueOrDefault(child.Name) ?? Array.Empty<string>();
                if (accepted.Length == 0)
                    continue;
                var preferred = accepted.FirstOrDefault(name => slots.ContainsKey(name)) ?? accepted[0];
                var describedTemplate = description?.Lines
                    .Select(line => line.Trim())
                    .FirstOrDefault(line => accepted.Any(name =>
                        line.Equals("," + name, StringComparison.OrdinalIgnoreCase) ||
                        line.StartsWith("," + name + " ", StringComparison.OrdinalIgnoreCase)));
                var existingTemplate = slots.GetValueOrDefault(preferred)?.Template;
                var template = string.IsNullOrWhiteSpace(describedTemplate)
                    ? existingTemplate
                    : describedTemplate;
                template = string.IsNullOrWhiteSpace(template)
                    ? "," + preferred
                    : template;
                slots[preferred] = new QuestCatalogChildSlot(
                    preferred,
                    child.Name,
                    QuestCatalogChildSlotKind.Child,
                    child.Quantity,
                    template,
                    accepted);
            }
        }

        if (definition.Name.Equals("BLOCK", StringComparison.OrdinalIgnoreCase))
        {
            slots["branch:path"] = new QuestCatalogChildSlot(
                "path",
                "Path",
                QuestCatalogChildSlotKind.Branch,
                PhaseBranchQuantity(phase, "path") ?? "+",
                "path",
                Array.Empty<string>());
        }

        if (autoExpand.TryGetValue(definition.Name, out var branches))
        {
            foreach (var branch in branches)
            {
                var quantity = PhaseBranchQuantity(phase, branch) ?? "?";
                slots["branch:" + branch] = new QuestCatalogChildSlot(
                    branch.ToLowerInvariant(),
                    Humanize(branch),
                    QuestCatalogChildSlotKind.Branch,
                    quantity,
                    branch.ToLowerInvariant(),
                    Array.Empty<string>());
            }
        }

        // A macro accepts an ordered body of nested macros when its EDF definition
        // declares ,BODY -- directly, or beneath an auto-expanded branch such as the
        // implicit path under ,BLOCK. PXSL lets that branch stay implicit, so the
        // authored source nests ordinary macros straight under the container. A ,BODY
        // whose phase only declares QuestObject children is a record list instead, so
        // it keeps its typed child slots and gains no ordered body.
        if (DeclaresOrderedBody(definition, phase) ||
            definition.Section.Equals("BaseCompound", StringComparison.OrdinalIgnoreCase) &&
            slots.Values.All(slot => slot.Kind != QuestCatalogChildSlotKind.Branch))
        {
            slots.TryAdd("body", new QuestCatalogChildSlot(
                "body",
                "Body",
                QuestCatalogChildSlotKind.OrderedBody,
                "*",
                string.Empty,
                Array.Empty<string>()));
        }

        return slots.Values
            .OrderBy(slot => slot.Kind)
            .ThenBy(slot => slot.Id, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static bool DeclaresOrderedBody(EdfDefinition definition, PhaseDefinition? phase)
    {
        if (!definition.Body.Any(line =>
                line.Trim().Equals(",BODY", StringComparison.OrdinalIgnoreCase)))
            return false;
        if (phase is null || phase.Children.Count == 0)
            return true;
        return phase.Children.Any(child =>
            child.Kind.Equals("path", StringComparison.OrdinalIgnoreCase));
    }

    private static string? PhaseBranchQuantity(PhaseDefinition? phase, string branch)
    {
        var matching = phase?.Children.FirstOrDefault(child =>
            child.Kind.Equals("path", StringComparison.OrdinalIgnoreCase) &&
            NormalizeBranchName(child.Name).Equals(branch, StringComparison.OrdinalIgnoreCase));
        return matching?.Quantity;
    }

    private static string NormalizeBranchName(string name)
    {
        var normalized = name.Replace("movie", string.Empty, StringComparison.OrdinalIgnoreCase)
            .Replace("_", string.Empty, StringComparison.Ordinal)
            .Replace("-", string.Empty, StringComparison.Ordinal)
            .Trim();
        if (normalized.Contains("fallback", StringComparison.OrdinalIgnoreCase))
            return "fallback";
        if (normalized.Contains("pre", StringComparison.OrdinalIgnoreCase))
            return "pre";
        if (normalized.Contains("post", StringComparison.OrdinalIgnoreCase))
            return "post";
        return "path";
    }

    private static QuestCatalogFunctionalCategory Category(
        EdfDefinition definition,
        string? resolvedPhaseType,
        IReadOnlyList<QuestCatalogChildSlot> childSlots)
    {
        if (definition.Section.Equals("Forbidden", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFunctionalCategory.SupportingDefinition;
        if (definition.Section.Equals("SubPhase", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFunctionalCategory.ChildRecords;

        var identity = (definition.Name + " " + resolvedPhaseType).ToLowerInvariant();
        if (ContainsAny(identity, "dialog", "movie", "cinematic", "conversation", "speaker", "talk"))
            return QuestCatalogFunctionalCategory.DialogueAndCinematics;
        if (ContainsAny(identity, "challenge", "mode", "difficulty", "race", "parkour", "timer"))
            return QuestCatalogFunctionalCategory.ChallengesAndModes;
        if (ContainsAny(identity, "player", "inventory", "reward", "item", "weapon", "skill", "stash", "loot"))
            return QuestCatalogFunctionalCategory.PlayerInventoryAndRewards;
        if (ContainsAny(identity, "objective", "guard", "checkpoint", "condition", "wait", "require"))
            return QuestCatalogFunctionalCategory.ObjectivesAndGuards;
        if (ContainsAny(identity, "ai", "actor", "zombie", "movement", "move", "patrol", "combat", "chase"))
            return QuestCatalogFunctionalCategory.AiAndMovement;
        if (ContainsAny(identity, "object", "world", "spawn", "light", "area", "trigger", "enable", "disable", "activate"))
            return QuestCatalogFunctionalCategory.WorldAndObjects;
        if (definition.Name.Equals("BLOCK", StringComparison.OrdinalIgnoreCase) ||
            definition.Name.Equals("quest", StringComparison.OrdinalIgnoreCase) ||
            resolvedPhaseType?.Equals("semaphore", StringComparison.OrdinalIgnoreCase) == true ||
            definition.Section.Contains("Compound", StringComparison.OrdinalIgnoreCase) ||
            childSlots.Any(slot => slot.Kind is QuestCatalogChildSlotKind.OrderedBody or QuestCatalogChildSlotKind.Branch))
            return QuestCatalogFunctionalCategory.FlowAndContainers;
        return QuestCatalogFunctionalCategory.CustomAndOther;
    }

    private static bool ContainsAny(string value, params string[] needles) =>
        needles.Any(needle => value.Contains(needle, StringComparison.OrdinalIgnoreCase));

    private static string ResolveTemplate(EdfDefinition definition, DescriptionDefinition? description)
    {
        var prefix = "," + definition.Name;
        var described = description?.Lines.FirstOrDefault(line =>
            line.TrimStart().StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
        if (!string.IsNullOrWhiteSpace(described))
            return described.Trim();
        if (definition.Parameters.Count == 0)
            return prefix;
        return prefix + " " + string.Join(' ', definition.Parameters.Select(parameter =>
            "[" + parameter.Name + "]"));
    }

    private static ResolvedStyle? ResolveStyle(
        string macroName,
        string? resolvedPhaseType,
        IReadOnlyDictionary<string, StyleDefinition> styles)
    {
        if (!string.IsNullOrWhiteSpace(resolvedPhaseType) &&
            styles.TryGetValue(resolvedPhaseType, out var phaseStyle))
            return new ResolvedStyle(phaseStyle.Style, phaseStyle.Source);
        if (styles.TryGetValue(macroName, out var macroStyle))
            return new ResolvedStyle(macroStyle.Style, macroStyle.Source);
        return null;
    }

    private static string? ResolvePhaseType(
        EdfDefinition definition,
        IReadOnlyDictionary<string, EdfDefinition> definitions)
    {
        var current = definition;
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (visited.Add(current.Name))
        {
            if (!string.IsNullOrWhiteSpace(current.DirectPhaseType))
                return current.DirectPhaseType;
            if (string.IsNullOrWhiteSpace(current.AliasTarget) ||
                !definitions.TryGetValue(current.AliasTarget, out current!))
                return null;
        }
        return null;
    }

    private static void ParseEdf(string text, string source, ExtractionState state)
    {
        var section = string.Empty;
        EdfBuilder? current = null;
        foreach (var line in SplitLines(text))
        {
            var sectionMatch = SectionPattern().Match(line);
            if (sectionMatch.Success)
            {
                Commit(current, state);
                current = null;
                section = sectionMatch.Groups["name"].Value.Trim();
                continue;
            }

            // Only column-zero definitions are catalog entries. Indented comma
            // invocations belong to the current macro body and are aliases/children.
            var definitionMatch = EdfDefinitionPattern().Match(line);
            if (definitionMatch.Success)
            {
                Commit(current, state);
                current = new EdfBuilder(
                    definitionMatch.Groups["name"].Value,
                    section,
                    ParseEdfParameters(definitionMatch.Groups["arguments"].Value),
                    source);
                continue;
            }
            current?.Body.Add(line);
        }
        Commit(current, state);
    }

    private static void Commit(EdfBuilder? builder, ExtractionState state)
    {
        if (builder is null || state.Macros.ContainsKey(builder.Name))
            return;
        string? directPhaseType = null;
        string? aliasTarget = null;
        string? producedObject = null;
        foreach (var rawLine in builder.Body)
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line.StartsWith('#'))
                continue;
            var phase = DirectPhasePattern().Match(line);
            if (phase.Success)
            {
                directPhaseType ??= TrimWrapped(phase.Groups["type"].Value);
                continue;
            }
            var alias = BodyMacroPattern().Match(line);
            if (alias.Success)
            {
                aliasTarget ??= alias.Groups["name"].Value;
                continue;
            }
            if (line.Equals(",BODY", StringComparison.OrdinalIgnoreCase) ||
                line is "path" or "fallback" or "pre" or "post" ||
                line.StartsWith("--", StringComparison.Ordinal))
                continue;
            var token = line.Split((char[]?)null, 2, StringSplitOptions.RemoveEmptyEntries)[0];
            if (builder.Section.Equals("SubPhase", StringComparison.OrdinalIgnoreCase) &&
                MacroNamePattern().IsMatch(token))
                producedObject ??= token;
        }
        state.Macros.TryAdd(builder.Name, new EdfDefinition(
            builder.Name,
            builder.Section,
            builder.Parameters,
            builder.Body.ToArray(),
            directPhaseType,
            aliasTarget,
            producedObject,
            builder.Source));
    }

    private static IReadOnlyList<QuestCatalogParameter> ParseEdfParameters(string text)
    {
        var result = new List<QuestCatalogParameter>();
        var tokens = text.Split((char[]?)null,
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        for (var index = 0; index < tokens.Length; ++index)
        {
            var name = tokens[index].Trim('[', ']', '<', '>', '(', ')', ',');
            if (name.Length == 0 || name == "_" || name == "\\")
                continue;
            result.Add(new QuestCatalogParameter(
                name,
                "string",
                true,
                result.Count,
                Array.Empty<string>()));
        }
        return result;
    }

    private static void ParseConfig(string text, string source, ExtractionState state)
    {
        string? currentBlock = null;
        foreach (var line in SplitLines(text))
        {
            var block = ConfigBlockPattern().Match(line);
            if (block.Success)
            {
                currentBlock = NormalizeName(block.Groups["name"].Value);
                state.AutoExpand.TryAdd(currentBlock, new List<string>());
                continue;
            }
            var expand = ConfigExpandPattern().Match(line);
            if (currentBlock is not null && expand.Success)
            {
                var branch = NormalizeName(expand.Groups["name"].Value).ToLowerInvariant();
                var branches = (List<string>)state.AutoExpand[currentBlock];
                if (!branches.Contains(branch, StringComparer.OrdinalIgnoreCase))
                    branches.Add(branch);
                state.AutoExpandSources.TryAdd(branch, source);
            }

        }

        foreach (var line in SplitLines(text))
        {
            var trimmed = line.TrimStart();
            if (!trimmed.StartsWith("Desc(", StringComparison.Ordinal))
                continue;
            var arguments = ParseQuotedArguments(trimmed[5..]);
            if (arguments.Count < 2)
                continue;
            var name = NormalizeName(arguments[0]);
            var descriptions = arguments.Skip(1)
                .Where(value => value.Length > 0)
                .ToArray();
            state.Descriptions.TryAdd(name, new DescriptionDefinition(name, descriptions, source));
        }
    }

    private static IReadOnlyList<string> ParseQuotedArguments(string text)
    {
        var result = new List<string>();
        var value = new StringBuilder();
        var quoted = false;
        var escaped = false;
        foreach (var character in text)
        {
            if (escaped)
            {
                value.Append(character);
                escaped = false;
                continue;
            }
            if (quoted && character == '\\')
            {
                escaped = true;
                continue;
            }
            if (character == '"')
            {
                if (quoted)
                {
                    result.Add(value.ToString());
                    value.Clear();
                }
                quoted = !quoted;
                continue;
            }
            if (quoted)
                value.Append(character);
        }
        return result;
    }

    private static void ParsePhaseDefinitions(byte[] bytes, string source, ExtractionState state)
    {
        var document = LoadXml(bytes);
        foreach (var phase in document.Descendants().Where(element =>
            element.Name.LocalName.Equals("PhaseType", StringComparison.OrdinalIgnoreCase)))
        {
            var name = phase.Attribute("name")?.Value?.Trim();
            if (string.IsNullOrWhiteSpace(name) || state.Phases.ContainsKey(name))
                continue;
            var parameters = new List<QuestCatalogParameter>();
            var children = new List<DefinitionChild>();
            foreach (var child in phase.Elements())
            {
                if (child.Name.LocalName.Equals("attribute", StringComparison.OrdinalIgnoreCase))
                {
                    var parameterName = child.Attribute("name")?.Value?.Trim();
                    if (string.IsNullOrWhiteSpace(parameterName))
                        continue;
                    parameters.Add(new QuestCatalogParameter(
                        parameterName,
                        child.Attribute("type")?.Value ?? "string",
                        !string.Equals(child.Attribute("optional")?.Value, "true", StringComparison.OrdinalIgnoreCase),
                        null,
                        child.Elements().Where(element =>
                            element.Name.LocalName.Equals("value", StringComparison.OrdinalIgnoreCase))
                            .Select(element => element.Value.Trim())
                            .Where(value => value.Length > 0)
                            .Distinct(StringComparer.Ordinal)
                            .ToArray()));
                    continue;
                }
                var childName = child.Attribute("name")?.Value?.Trim();
                if (string.IsNullOrWhiteSpace(childName))
                    continue;
                children.Add(new DefinitionChild(
                    childName,
                    child.Name.LocalName,
                    child.Attribute("quantity")?.Value?.Trim() ?? "?"));
            }
            state.Phases.TryAdd(name, new PhaseDefinition(name, parameters, children, source));
        }
    }

    private static void ParseObjectDefinitions(byte[] bytes, string source, ExtractionState state)
    {
        var document = LoadXml(bytes);
        foreach (var definition in document.Descendants().Where(element =>
            element.Name.LocalName.Equals("QuestObject", StringComparison.OrdinalIgnoreCase)))
        {
            var name = definition.Attribute("name")?.Value?.Trim();
            if (string.IsNullOrWhiteSpace(name) || state.Objects.ContainsKey(name))
                continue;
            var classes = definition.Elements().Where(element =>
                element.Name.LocalName.Equals("class", StringComparison.OrdinalIgnoreCase))
                .Select(element => element.Attribute("name")?.Value?.Trim())
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Select(name => name!)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray();
            state.Objects.TryAdd(name, new ObjectDefinition(name, classes, source));
        }
    }

    private static void ParseColors(byte[] bytes, string source, ExtractionState state)
    {
        var document = LoadXml(bytes);
        foreach (var phase in document.Descendants().Where(element =>
            element.Name.LocalName.Equals("PhaseType", StringComparison.OrdinalIgnoreCase)))
        {
            var name = phase.Attribute("name")?.Value?.Trim();
            if (string.IsNullOrWhiteSpace(name) || state.Styles.ContainsKey(name))
                continue;
            state.Styles.TryAdd(name, new StyleDefinition(
                name,
                new QuestCatalogStyle(
                    NormalizeColor(phase.Attribute("color")?.Value),
                    NormalizeColor(phase.Attribute("font-color")?.Value),
                    phase.Attribute("short")?.Value?.Trim()),
                source));
        }
    }

    private static string? NormalizeColor(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return null;
        var color = value.Trim().TrimStart('#');
        if (color.Length is 3 or 6 or 8 && color.All(Uri.IsHexDigit))
            return "#" + color.ToUpperInvariant();

        var components = color.Split((char[]?)null,
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (components.Length is 3 or 4 && components.All(component =>
            double.TryParse(component, NumberStyles.Float, CultureInfo.InvariantCulture, out _)))
        {
            var bytes = components.Select(component =>
            {
                var parsed = double.Parse(component, CultureInfo.InvariantCulture);
                return (byte)Math.Clamp((int)Math.Round(parsed <= 1 ? parsed * 255 : parsed), 0, 255);
            }).ToArray();
            return bytes.Length == 4
                ? $"#{bytes[3]:X2}{bytes[0]:X2}{bytes[1]:X2}{bytes[2]:X2}"
                : "#" + Convert.ToHexString(bytes);
        }
        return null;
    }

    private static XDocument LoadXml(byte[] bytes)
    {
        using var stream = new MemoryStream(bytes, writable: false);
        using var reader = XmlReader.Create(stream, new XmlReaderSettings
        {
            DtdProcessing = DtdProcessing.Prohibit,
            XmlResolver = null,
            IgnoreComments = true,
            MaxCharactersInDocument = MaximumInputBytes
        });
        return XDocument.Load(reader, LoadOptions.None);
    }

    private static QuestCatalogFormat ResolveFormat(
        string name,
        QuestCatalogFormat requested,
        byte[] bytes)
    {
        if (requested != QuestCatalogFormat.Auto)
            return requested;
        var extension = Path.GetExtension(name);
        if (extension.Equals(".edf", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.Edf;
        if (Path.GetFileName(name).Contains("QuestEditorConfig", StringComparison.OrdinalIgnoreCase))
            return QuestCatalogFormat.QuestEditorConfig;
        if (extension.Equals(".xml", StringComparison.OrdinalIgnoreCase))
        {
            var document = LoadXml(bytes);
            return document.Root?.Name.LocalName switch
            {
                "QuestObjectDefinitions" => QuestCatalogFormat.QuestObjectDefinitionsXml,
                "ColorDefs" => QuestCatalogFormat.QuestColorsXml,
                "PhaseTypeDefinitions" => QuestCatalogFormat.PhaseDefinitionsXml,
                _ => throw new InvalidDataException(
                    $"Could not infer quest catalog XML format for '{Path.GetFileName(name)}'.")
            };
        }
        throw new InvalidDataException($"Could not infer quest catalog format for '{Path.GetFileName(name)}'.");
    }

    private static byte[] ReadBounded(Stream stream, int maximum)
    {
        using var memory = new MemoryStream();
        var buffer = new byte[81920];
        while (true)
        {
            var read = stream.Read(buffer, 0, buffer.Length);
            if (read == 0)
                break;
            if (memory.Length + read > maximum)
                throw new InvalidDataException($"Quest catalog input exceeds {maximum} bytes.");
            memory.Write(buffer, 0, read);
        }
        return memory.ToArray();
    }

    private static string DecodeText(byte[] bytes)
    {
        var encoding = new UTF8Encoding(false, true);
        if (bytes.AsSpan().StartsWith(Encoding.UTF8.GetPreamble()))
            return encoding.GetString(bytes.AsSpan(Encoding.UTF8.GetPreamble().Length));
        return encoding.GetString(bytes);
    }

    private static IEnumerable<string> SplitLines(string text) =>
        text.Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Split('\n');

    private static string TrimWrapped(string value)
    {
        var trimmed = value.Trim();
        if (trimmed.StartsWith("<<", StringComparison.Ordinal) &&
            trimmed.EndsWith(">>", StringComparison.Ordinal) &&
            trimmed.Length >= 4)
            return trimmed[2..^2].Trim();
        return trimmed.Trim('"');
    }

    private static string NormalizeName(string name) => name.Trim().TrimStart(',');

    private static string Humanize(string value) =>
        value.Length == 0 ? value : char.ToUpperInvariant(value[0]) + value[1..];

    private static IReadOnlyList<string> Sources(params string?[] sources) => sources
        .Where(source => !string.IsNullOrWhiteSpace(source))
        .Select(source => source!)
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .OrderBy(source => source, StringComparer.OrdinalIgnoreCase)
        .ToArray();

    private static string CatalogHash(
        IReadOnlyList<QuestCatalogEntry> entries,
        IReadOnlyList<QuestCatalogProvenance> provenance)
    {
        var canonical = new StringBuilder();
        foreach (var item in provenance)
            canonical.Append(item.Name).Append('|').Append(item.Format).Append('|').Append(item.Sha256).Append('\n');
        foreach (var entry in entries)
        {
            canonical.Append(entry.Kind).Append('|').Append(entry.Name).Append('|')
                .Append(entry.IsInsertable).Append('|').Append(entry.IsCompileEligible).Append('|')
                .Append(entry.Category).Append('|').Append(entry.DefinitionSection).Append('|')
                .Append(entry.ResolvedPhaseType).Append('|').Append(entry.AliasTarget).Append('|')
                .Append(entry.Template).Append('\n');
            if (entry.Style is not null)
                canonical.Append("S|").Append(entry.Style.HeaderColor).Append('|')
                    .Append(entry.Style.FontColor).Append('|').Append(entry.Style.ShortName).Append('\n');
            foreach (var parameter in entry.Parameters)
                canonical.Append("P|").Append(parameter.Position).Append('|').Append(parameter.Name)
                    .Append('|').Append(parameter.Type).Append('|').Append(parameter.IsRequired)
                    .Append('|').AppendJoin(',', parameter.Values).Append('\n');
            foreach (var field in entry.InspectorFields)
                canonical.Append("I|").Append(field.Id).Append('|').Append(field.ParameterName)
                    .Append('|').Append(field.EditorKind).Append('|').Append(field.Role)
                    .Append('|').Append(field.TypeName).Append('|').Append(field.IsRequired)
                    .Append('|').Append(field.Position).Append('|').Append(field.NamedArgument)
                    .Append('|').AppendJoin(',', field.AllowedValues).Append('|')
                    .Append(field.PairedFieldId).Append('|').Append(field.Description)
                    .Append('\n');
            foreach (var slot in entry.ChildSlots)
                canonical.Append("C|").Append(slot.Id).Append('|').Append(slot.Kind).Append('|')
                    .Append(slot.Quantity).Append('|').Append(slot.Template).Append('|')
                    .AppendJoin(',', slot.AcceptedEntryNames).Append('\n');
        }
        return QuestHash.Sha256(canonical.ToString());
    }

    [GeneratedRegex("^###\\s*(?<name>.*?)\\s*###\\s*$", RegexOptions.CultureInvariant)]
    private static partial Regex SectionPattern();

    [GeneratedRegex("^,(?<name>[A-Za-z0-9_.-]+)(?<arguments>.*?)=\\s*$", RegexOptions.CultureInvariant)]
    private static partial Regex EdfDefinitionPattern();

    [GeneratedRegex("^phase\\s+(?:-type=)?(?<type><<.*?>>|[^\\s]+)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex DirectPhasePattern();

    [GeneratedRegex("^,(?<name>[A-Za-z0-9_.-]+)(?:\\s|$)", RegexOptions.CultureInvariant)]
    private static partial Regex BodyMacroPattern();

    [GeneratedRegex("^,(?<name>[A-Za-z0-9_.-]+)(?:\\s|$)", RegexOptions.CultureInvariant)]
    private static partial Regex ChildMacroPattern();

    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_.-]*$", RegexOptions.CultureInvariant)]
    private static partial Regex MacroNamePattern();

    [GeneratedRegex("Block\\(\\s*\"(?<name>[^\"]+)\"\\s*\\)", RegexOptions.CultureInvariant)]
    private static partial Regex ConfigBlockPattern();

    [GeneratedRegex("Expand\\(\\s*\"(?<name>[^\"]+)\"\\s*\\)", RegexOptions.CultureInvariant)]
    private static partial Regex ConfigExpandPattern();

    private sealed class ExtractionState
    {
        public Dictionary<string, EdfDefinition> Macros { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, DescriptionDefinition> Descriptions { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, IReadOnlyList<string>> AutoExpand { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, string> AutoExpandSources { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, PhaseDefinition> Phases { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, ObjectDefinition> Objects { get; } = new(StringComparer.OrdinalIgnoreCase);
        public Dictionary<string, StyleDefinition> Styles { get; } = new(StringComparer.OrdinalIgnoreCase);
    }

    private sealed class EdfBuilder(
        string name,
        string section,
        IReadOnlyList<QuestCatalogParameter> parameters,
        string source)
    {
        public string Name { get; } = name;
        public string Section { get; } = section;
        public IReadOnlyList<QuestCatalogParameter> Parameters { get; } = parameters;
        public string Source { get; } = source;
        public List<string> Body { get; } = new();
    }

    private sealed record EdfDefinition(
        string Name,
        string Section,
        IReadOnlyList<QuestCatalogParameter> Parameters,
        IReadOnlyList<string> Body,
        string? DirectPhaseType,
        string? AliasTarget,
        string? ProducedObjectType,
        string Source);

    private sealed record DescriptionDefinition(
        string Name,
        IReadOnlyList<string> Lines,
        string Source);

    private sealed record DefinitionChild(string Name, string Kind, string Quantity);

    private sealed record PhaseDefinition(
        string Name,
        IReadOnlyList<QuestCatalogParameter> Parameters,
        IReadOnlyList<DefinitionChild> Children,
        string Source);

    private sealed record ObjectDefinition(
        string Name,
        IReadOnlyList<string> Classes,
        string Source);

    private sealed record StyleDefinition(
        string Name,
        QuestCatalogStyle Style,
        string Source);

    private sealed record ResolvedStyle(QuestCatalogStyle Style, string Source);
}
