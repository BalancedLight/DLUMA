using System.Globalization;
using System.Text;

namespace Dluma.QuestGraph.Core;

internal sealed record QuestCatalogAuthoringMetadata(
    string Template,
    IReadOnlyDictionary<string, PxslArgumentToken> Bindings)
{
    public const string CommentPrefix = "# DLUMA authoring: ";

    public static QuestCatalogAuthoringMetadata? Parse(
        string name,
        IReadOnlyList<QuestCatalogParameter> parameters,
        string? template)
    {
        if (string.IsNullOrWhiteSpace(template) || template.Length > 16_384 ||
            template.IndexOfAny(['\r', '\n']) >= 0)
            return null;
        var tokens = PxslSourceTokenizer.Tokenize(template,
            new PxslTextSpan(0, Encoding.UTF8.GetByteCount(template), 1, 1), Encoding.UTF8);
        if (tokens.Count == 0 || !tokens[0].Text.Equals("," + name, StringComparison.Ordinal))
            return null;
        var arguments = PxslSourceTokenizer.Arguments(tokens, 1, Encoding.UTF8);
        if (arguments.Count != parameters.Count)
            return null;
        var bindings = new Dictionary<string, PxslArgumentToken>(StringComparer.OrdinalIgnoreCase);
        foreach (var argument in arguments)
        {
            var parameter = argument.IsNamed
                ? parameters.FirstOrDefault(value => value.Name.Equals(
                    argument.NamedArgument, StringComparison.OrdinalIgnoreCase))
                : parameters.FirstOrDefault(value => value.Position == argument.Position);
            if (parameter is null || argument.Value.Length == 0 ||
                !bindings.TryAdd(parameter.Name, argument))
                return null;
        }
        return bindings.Count == parameters.Count
            ? new QuestCatalogAuthoringMetadata(template, bindings)
            : null;
    }

    public IReadOnlyList<QuestInspectorFieldDefinition> Apply(
        IReadOnlyList<QuestInspectorFieldDefinition> fields) => fields.Select(field =>
    {
        var binding = Bindings[field.ParameterName];
        var numeric = !binding.IsWrapped && double.TryParse(binding.Value,
            NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out var number) &&
            double.IsFinite(number);
        return field with
        {
            Position = binding.Position,
            NamedArgument = binding.NamedArgument,
            IsRequired = true,
            AuthoringOrder = Bindings.Values.OrderBy(value => value.Span.Start)
                .ToList().FindIndex(value => value == binding),
            RequiresAngleBrackets = PxslValueWrapping.Detect(
                PxslValueWrapping.RawValue(binding)) == PxslValueWrapper.AngleBrackets,
            EditorKind = numeric ? QuestParameterEditorKind.Number : QuestParameterEditorKind.Text,
            TypeName = numeric ? "float" : "string"
        };
    }).ToArray();
}
