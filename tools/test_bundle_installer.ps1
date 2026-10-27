[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$loaderVersion = (& (Join-Path $repoRoot 'tools\get_loader_version.ps1')).Trim()
$modFileName = "Dying Light Universal Mod Loader ($loaderVersion).mod"
$fixture = Join-Path $repoRoot ('build\installer_fixture_' + [Guid]::NewGuid().ToString('N'))
$loader = Join-Path $fixture 'loader_item'
$mesmerized = Join-Path $fixture 'Mesmerized'
$extract = Join-Path $fixture 'bundle'

New-Item -ItemType Directory -Path (Join-Path $loader 'mods'),(Join-Path $mesmerized 'data\maps\nowhere') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "bin\x64\Release\$modFileName") -Destination (Join-Path $loader 'mods\selected_loader_name.mod')
Expand-Archive -LiteralPath (Join-Path $repoRoot 'release\DL-Universal-Mod-Loader-0.1.4-Lifecycle-XUI-Probe.zip') -DestinationPath $extract
$bundle = Join-Path $extract 'DL-Universal-Mod-Loader-0.1.4-Test'

& (Join-Path $bundle 'INSTALL_0.1.4_TEST.ps1') -LoaderItemRoot $loader -MesmerizedRoot $mesmerized
$expected = @(
    (Join-Path $loader 'mods\selected_loader_name.mod.before_0.1.4.bak'),
    (Join-Path $loader 'mods\dl_universal_loader.pak'),
    (Join-Path $loader 'data\scripts\dlw\global_loader_status.lua'),
    (Join-Path $mesmerized 'data\scripts\dlw\mesmerized_sensor_interaction_test.lua'),
    (Join-Path $mesmerized 'data\scripts\workshop_lifecycle_test.dlscript')
)
foreach ($path in $expected) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing installed fixture file: $path"
    }
}

& (Join-Path $bundle 'RESTORE_PREVIOUS_MODULE.ps1') -LoaderItemRoot $loader
if (Test-Path -LiteralPath (Join-Path $loader 'mods\dl_universal_loader.pak')) {
    throw 'Restore did not remove the fixture pack.'
}

Write-Output "Installer fixture test: PASS ($fixture)"
