param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [ValidateRange(1000, 60000)]
    [int]$TimeoutMilliseconds = 10000
)

$ErrorActionPreference = "Stop"
$packageRoot = (Resolve-Path -LiteralPath $PackageDir).Path
$runtimeRoot = Join-Path $packageRoot "dluma_runtime"
$dotnetRoot = Join-Path $runtimeRoot "dotnet"
$executable = Join-Path $runtimeRoot "quest-graph\Dluma.QuestGraph.exe"
$manifest = Join-Path $runtimeRoot "runtime-manifest.json"

foreach ($required in @($dotnetRoot, $executable, $manifest)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Packaged visual quest editor prerequisite is missing: $required"
    }
}

$environmentNames = @(
    "DOTNET_ROOT_X64",
    "DOTNET_ROOT",
    "DOTNET_MULTILEVEL_LOOKUP"
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable(
        $name,
        [EnvironmentVariableTarget]::Process
    )
}

$process = $null
try {
    [Environment]::SetEnvironmentVariable(
        "DOTNET_ROOT_X64",
        $dotnetRoot,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        "DOTNET_ROOT",
        $dotnetRoot,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        "DOTNET_MULTILEVEL_LOOKUP",
        "0",
        [EnvironmentVariableTarget]::Process
    )
    $process = Start-Process -FilePath $executable -ArgumentList "--smoke-test" -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($TimeoutMilliseconds)) {
        throw "The packaged visual quest editor did not exit within the timeout."
    }
    if ($process.ExitCode -ne 0) {
        throw "The packaged visual quest editor exited with code $($process.ExitCode)."
    }
    Write-Host "Packaged visual quest editor started with its private runtime and exited cleanly."
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $savedEnvironment[$name],
            [EnvironmentVariableTarget]::Process
        )
    }
}
