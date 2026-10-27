[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"

function Get-NormalizedFullPath([string]$PathValue) {
    return [System.IO.Path]::GetFullPath($PathValue).TrimEnd('\', '/')
}

function Test-PathWithin([string]$Child, [string]$Parent) {
    $parentPrefix = $Parent.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    return $Child.StartsWith($parentPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-SafeChild([string]$Child, [string]$Parent, [string]$Label) {
    if (-not (Test-PathWithin $Child $Parent)) {
        throw "$Label escaped its intended parent directory."
    }
}

function Get-PackageHashes([string]$Root) {
    $prefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $hashes = @{}
    Get-ChildItem -LiteralPath $Root -File -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        $hashes[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    return $hashes
}

function Assert-HashParity([hashtable]$Expected, [hashtable]$Actual) {
    if ($Expected.Count -ne $Actual.Count) {
        throw "Deployed package file count differs from the validated source."
    }
    foreach ($relative in $Expected.Keys) {
        if (-not $Actual.ContainsKey($relative) -or $Actual[$relative] -ne $Expected[$relative]) {
            throw "Deployed package hash differs for '$relative'."
        }
    }
}

$sourceItem = Get-Item -LiteralPath $Source -Force
if (-not $sourceItem.PSIsContainer) {
    throw "Source must be a package directory."
}
$sourceFull = Get-NormalizedFullPath $sourceItem.FullName
$destinationFull = Get-NormalizedFullPath $Destination
$destinationRoot = [System.IO.Path]::GetPathRoot($destinationFull).TrimEnd('\', '/')
if ($destinationFull -eq $destinationRoot) {
    throw "Destination must be an exact package directory, not a volume root."
}
if ((Test-PathWithin $destinationFull $sourceFull) -or
    (Test-PathWithin $sourceFull $destinationFull) -or
    $sourceFull.Equals($destinationFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Source and destination must not contain one another."
}

$destinationParent = Split-Path -Parent $destinationFull
if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
}
$destinationParent = Get-NormalizedFullPath (Get-Item -LiteralPath $destinationParent -Force).FullName
$leaf = Split-Path -Leaf $destinationFull
$nonce = [Guid]::NewGuid().ToString('N')
$stageRoot = Join-Path $destinationParent (".{0}.{1}.stage" -f $leaf, $nonce)
$stage = Join-Path $stageRoot ("mods\native\global\{0}" -f $leaf)
$backup = Join-Path $destinationParent (".{0}.{1}.backup" -f $leaf, $nonce)
$failed = Join-Path $destinationParent (".{0}.{1}.failed" -f $leaf, $nonce)
Assert-SafeChild $stageRoot $destinationParent "Stage root"
Assert-SafeChild $stage $stageRoot "Stage package path"
Assert-SafeChild $backup $destinationParent "Backup path"
Assert-SafeChild $failed $destinationParent "Failure path"

$validator = Join-Path $PSScriptRoot 'validate_mod_package.py'
& python $validator validate $sourceFull --require-lock | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Source package validation failed."
}
$sourceHashes = Get-PackageHashes $sourceFull

$oldMoved = $false
$newMoved = $false
try {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceFull -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stage -Recurse -Force
    }
    & python $validator validate $stage --require-lock | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Staged package validation failed."
    }
    Assert-HashParity $sourceHashes (Get-PackageHashes $stage)

    if (Test-Path -LiteralPath $destinationFull) {
        Move-Item -LiteralPath $destinationFull -Destination $backup
        $oldMoved = $true
    }
    Move-Item -LiteralPath $stage -Destination $destinationFull
    $newMoved = $true

    & python $validator validate $destinationFull --require-lock | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Installed package validation failed."
    }
    Assert-HashParity $sourceHashes (Get-PackageHashes $destinationFull)

    if ($oldMoved) {
        Remove-Item -LiteralPath $backup -Recurse -Force
        $oldMoved = $false
    }
    Write-Host "Deployed and verified: $destinationFull"
}
catch {
    if ($newMoved -and (Test-Path -LiteralPath $destinationFull)) {
        Move-Item -LiteralPath $destinationFull -Destination $failed
        $newMoved = $false
    }
    if ($oldMoved -and (Test-Path -LiteralPath $backup)) {
        Move-Item -LiteralPath $backup -Destination $destinationFull
        $oldMoved = $false
    }
    if (Test-Path -LiteralPath $failed) {
        Remove-Item -LiteralPath $failed -Recurse -Force
    }
    throw
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}
