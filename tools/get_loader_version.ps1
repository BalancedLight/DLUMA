[CmdletBinding()]
param(
    [string]$HeaderPath
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ([string]::IsNullOrWhiteSpace($HeaderPath)) {
    $HeaderPath = Join-Path $scriptRoot '..\src\dlw_common.h'
}
$header = [System.IO.File]::ReadAllText([System.IO.Path]::GetFullPath($HeaderPath))
$match = [System.Text.RegularExpressions.Regex]::Match(
    $header,
    '(?m)^\s*#define\s+DLUMA_VERSION\s+"(?<version>\d+(?:\.\d+)+)"\s*$')
if (-not $match.Success) {
    throw "Could not find a numeric DLUMA_VERSION in $HeaderPath."
}

$match.Groups['version'].Value
