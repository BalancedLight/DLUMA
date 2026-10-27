using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Dluma.Managed.Analyzers;

[DiagnosticAnalyzer(LanguageNames.CSharp)]
public sealed class DlumaModAnalyzer : DiagnosticAnalyzer
{
    public const string AsyncLifecycleId = "DLUMA1001";
    public const string NativeBypassId = "DLUMA1002";

    private static readonly DiagnosticDescriptor AsyncLifecycle = new(
        AsyncLifecycleId,
        "DLUMA lifecycle methods must finish synchronously",
        "'{0}' must not be async; schedule bounded work through the DLUMA API",
        "DyingLightWorkbench",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true,
        description:
            "DLUMA revokes owner resources when a lifecycle callback returns, so async void lifecycle methods are unsafe.");

    private static readonly DiagnosticDescriptor NativeBypass = new(
        NativeBypassId,
        "Native interop bypasses DLUMA access enforcement",
        "'{0}' uses direct native interop; this bypasses DLUMA service access declarations",
        "DyingLightWorkbench",
        DiagnosticSeverity.Warning,
        isEnabledByDefault: true,
        description:
            "Managed mods are full trust. Direct native interop is allowed but is not governed by the manifest access declaration.");

    public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics =>
        [AsyncLifecycle, NativeBypass];

    public override void Initialize(AnalysisContext context)
    {
        context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
        context.EnableConcurrentExecution();
        context.RegisterSyntaxNodeAction(
            AnalyzeMethod,
            SyntaxKind.MethodDeclaration);
        context.RegisterSyntaxNodeAction(
            AnalyzeAttribute,
            SyntaxKind.Attribute);
    }

    private static void AnalyzeMethod(SyntaxNodeAnalysisContext context)
    {
        var method = (Microsoft.CodeAnalysis.CSharp.Syntax.MethodDeclarationSyntax)
            context.Node;
        var name = method.Identifier.ValueText;
        if (name is not ("OnStart" or "OnStop") ||
            !method.Modifiers.Any(SyntaxKind.AsyncKeyword))
        {
            return;
        }

        var symbol = context.SemanticModel.GetDeclaredSymbol(method);
        if (symbol?.ContainingType.AllInterfaces.Any(
                item => item.ToDisplayString() == "Dluma.Managed.Sdk.IDlumaMod") != true)
        {
            return;
        }

        context.ReportDiagnostic(
            Diagnostic.Create(
                AsyncLifecycle,
                method.Identifier.GetLocation(),
                name));
    }

    private static void AnalyzeAttribute(SyntaxNodeAnalysisContext context)
    {
        var attribute =
            (Microsoft.CodeAnalysis.CSharp.Syntax.AttributeSyntax)context.Node;
        var symbol = context.SemanticModel.GetSymbolInfo(attribute).Symbol
            as IMethodSymbol;
        var containingType = symbol?.ContainingType.ToDisplayString();
        if (containingType is not (
            "System.Runtime.InteropServices.DllImportAttribute" or
            "System.Runtime.InteropServices.LibraryImportAttribute"))
        {
            return;
        }

        context.ReportDiagnostic(
            Diagnostic.Create(
                NativeBypass,
                attribute.GetLocation(),
                attribute.Name.ToString()));
    }
}
