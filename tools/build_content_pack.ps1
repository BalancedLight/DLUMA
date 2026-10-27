[CmdletBinding()]
param(
    [string]$OutputPath,
    [string]$QuestBindings,
    [string[]]$QuestXml,
    [string]$QuestProjectRoot,
    [string]$QuestProject,
    [string]$QuestMap
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$python = Get-Command python -ErrorAction Stop
$builder = Join-Path $repoRoot 'tools\build_content_pack.py'

# Build and validate the content archive through the shared Python implementation.
$builderArgs = @($builder)
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $builderArgs += @('--output', [System.IO.Path]::GetFullPath($OutputPath))
}
if (-not [string]::IsNullOrWhiteSpace($QuestBindings)) {
    $builderArgs += @('--quest-bindings', [System.IO.Path]::GetFullPath($QuestBindings))
}
foreach ($questXmlPath in $QuestXml) {
    if ([string]::IsNullOrWhiteSpace($questXmlPath)) {
        throw 'Quest binding preflight does not accept an empty -QuestXml path.'
    }
    $builderArgs += @('--quest-xml', [System.IO.Path]::GetFullPath($questXmlPath))
}
if (-not [string]::IsNullOrWhiteSpace($QuestProjectRoot)) {
    $builderArgs += @('--quest-project-root', [System.IO.Path]::GetFullPath($QuestProjectRoot))
}
if (-not [string]::IsNullOrWhiteSpace($QuestProject)) {
    $builderArgs += @('--quest-project', $QuestProject)
}
if (-not [string]::IsNullOrWhiteSpace($QuestMap)) {
    $builderArgs += @('--quest-map', $QuestMap)
}

& $python.Source @builderArgs
if ($LASTEXITCODE -ne 0) {
    throw "Content-pack build failed with exit code $LASTEXITCODE."
}
