[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ModulePath,
    [string]$StageDirectory,
    [string]$ManifestSha256,
    [string]$ResultPath,
    [Parameter(Mandatory=$true)][string]$PrivateRoot,
    [Parameter(Mandatory=$true)][int]$PlayerPid,
    [Parameter(Mandatory=$true)][long]$PlayerStartTicks,
    [Parameter(Mandatory=$true)][ValidateSet('Update','Restore')][string]$Action
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Get-PinnedPlayer([int]$id, [long]$ticks) {
    try { $process = [Diagnostics.Process]::GetProcessById($id) }
    catch {
        if ($_.Exception.InnerException -is [ArgumentException]) { return $null }
        throw
    }
    try {
        [void]$process.Handle
        $actualTicks = $process.StartTime.ToUniversalTime().Ticks
    } catch {
        $process.Dispose()
        # If it exited during capture, only proceed when the PID is absent.
        try { $again = [Diagnostics.Process]::GetProcessById($id) }
        catch {
            if ($_.Exception.InnerException -is [ArgumentException]) { return $null }
            throw
        }
        $again.Dispose()
        throw 'Could not confirm process identity.'
    }
    if ($actualTicks -ne $ticks) { $process.Dispose(); throw 'Process identity does not match.' }
    return $process
}
function Full-Directory([string]$path) {
    $p = [IO.Path]::GetFullPath($path).TrimEnd('\')
    $d = New-Object IO.DirectoryInfo($p)
    if (!$d.Exists) { throw 'Required directory is missing.' }
    while ($null -ne $d) {
        if (($d.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Directory contains a reparse point.' }
        $d = $d.Parent
    }
    return $p
}
function Resolve-File([string]$root, [string]$relative) {
    if (!$relative -or $relative.Length -gt 240 -or $relative.Contains('\') -or $relative.Contains(':') -or [IO.Path]::IsPathRooted($relative)) { throw 'Invalid package path.' }
    $p = $root
    foreach ($part in $relative.Split('/')) {
        if (!$part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Invalid package path.' }
        $p = [IO.Path]::Combine($p, $part)
        if (([IO.File]::Exists($p) -or [IO.Directory]::Exists($p)) -and (([IO.File]::GetAttributes($p) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { throw 'Package path contains a reparse point.' }
    }
    return $p
}
function File-Hash([string]$path) {
    if ([IO.Directory]::Exists($path)) { throw 'Expected a file.' }
    if (![IO.File]::Exists($path)) { return $null }
    if (([IO.File]::GetAttributes($path) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'File is a reparse point.' }
    $stream = [IO.File]::Open($path, 'Open', 'Read', 'Read')
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') } finally { $sha.Dispose(); $stream.Dispose() }
}
function Assert-Hash([string]$path, $expected) {
    if ((File-Hash $path) -cne $expected) { throw 'File does not match its expected contents.' }
}
function Copy-Checked([string]$source, [string]$dest, [string]$hash) {
    Assert-Hash $source $hash
    $inputFile = [IO.File]::Open($source, 'Open', 'Read', 'Read')
    try {
        $outputFile = [IO.File]::Open($dest, 'CreateNew', 'Write', 'None')
        try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
    } finally { $inputFile.Dispose() }
    Assert-Hash $dest $hash
}
function Write-New([string]$path, [string]$text) {
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($text)
    $file = [IO.File]::Open($path, 'CreateNew', 'Write', 'None')
    try { $file.Write($bytes, 0, $bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
}
function Write-Result([string]$path, [bool]$success, [string]$action, [string]$commit) {
    $directory = [IO.Path]::GetDirectoryName($path)
    $temp = [IO.Path]::Combine($directory, '.dluma-result-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $displaced = $temp + '.old'
    $text = "[result]`r`nsuccess=$(if ($success) { '1' } else { '0' })`r`naction=$action`r`ncommit=$commit`r`nerror=$(if ($success) { '' } else { 'Installation failed.' })`r`n"
    $encoding = [Text.Encoding]::Unicode
    $bytes = $encoding.GetPreamble() + $encoding.GetBytes($text)
    try {
        $file = [IO.File]::Open($temp, 'CreateNew', 'Write', 'None')
        try { $file.Write($bytes, 0, $bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
        if ([IO.File]::Exists($path)) {
            if (([IO.File]::GetAttributes($path) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Result file is a reparse point.' }
            [IO.File]::Replace($temp, $path, $displaced)
        } else { [IO.File]::Move($temp, $path) }
    } finally { [IO.File]::Delete($temp); [IO.File]::Delete($displaced) }
}
function Read-Json([string]$path, [long]$limit) {
    if (!(File-Hash $path) -or (Get-Item -LiteralPath $path).Length -gt $limit) { throw 'Invalid JSON file.' }
    $utf8 = New-Object Text.UTF8Encoding($false, $true)
    return ($utf8.GetString([IO.File]::ReadAllBytes($path)) | ConvertFrom-Json)
}
function Runtime-Files([string]$root) {
    $result = @{}
    $name = 'dluma_runtime/runtime-manifest.json'
    $path = Resolve-File $root $name
    $data = Read-Json $path 1048576
    if ($data.schema -cne 'dluma.runtime-tree.v1' -or @($data.files).Count -lt 1 -or @($data.files).Count -gt 2048) { throw 'Invalid runtime manifest.' }
    $result[$name] = File-Hash $path
    foreach ($item in $data.files) {
        $relative = 'dluma_runtime/' + $item.path
        if (@($relative.Split('/') | Where-Object { $_ -ieq 'dluma.ini' }).Count -gt 0 -or $item.sha256 -cnotmatch '^[0-9A-F]{64}$' -or $item.size -isnot [ValueType] -or $item.size -le 0 -or $result.ContainsKey($relative)) { throw 'Invalid runtime file record.' }
        $file = Resolve-File $root $relative
        if (!(File-Hash $file) -or (Get-Item -LiteralPath $file).Length -ne $item.size) { throw 'Runtime file size does not match.' }
        Assert-Hash $file $item.sha256
        $result[$relative] = $item.sha256
    }
    return $result
}
function Check-Private([string]$root) {
    $acl = (New-Object IO.DirectoryInfo($root)).GetAccessControl()
    $current = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $admins = New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')
    $system = New-Object Security.Principal.SecurityIdentifier('S-1-5-18')
    $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier])
    if (!$owner.Equals($current) -and !$owner.Equals($admins)) { throw 'Updater directory owner is not trusted.' }
    foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
        if ($rule.AccessControlType -eq 'Allow' -and !$rule.IdentityReference.Equals($current) -and !$rule.IdentityReference.Equals($admins) -and !$rule.IdentityReference.Equals($system)) { throw 'Updater directory grants access to another account or group.' }
    }
}
function Read-Link([string]$path) {
    $x = Read-Json $path 4096
    if ($x.Version -ne 1 -or $x.Id -cnotmatch '^[0-9a-f]{32}$' -or ($null -ne $x.Previous -and $x.Previous -cnotmatch '^[0-9a-f]{32}$') -or $x.Commit -cnotmatch '^[0-9a-f]{40}$' -or [IO.Path]::GetFileName($path) -cne ('chain-' + $x.Id + '.json') -or ![IO.Path]::IsPathRooted($x.Target)) { throw 'Invalid backup chain entry.' }
    return $x
}
function Journal([string]$transaction, [string]$target) {
    [void](Full-Directory $transaction)
    $j = Read-Json (Resolve-File $transaction 'journal.json') 1048576
    if ($j.Version -ne 1 -or $j.Target -ine $target -or @($j.Changes).Count -lt 1 -or @($j.Changes).Count -gt 2048) { throw 'Invalid transaction journal.' }
    $seen = @{}
    foreach ($c in $j.Changes) {
        [void](Resolve-File $target $c.Path)
        if ($seen.ContainsKey($c.Path) -or ($null -ne $c.BeforeSha256 -and $c.BeforeSha256 -cnotmatch '^[0-9A-F]{64}$') -or ($null -ne $c.AfterSha256 -and $c.AfterSha256 -cnotmatch '^[0-9A-F]{64}$') -or ($null -eq $c.BeforeSha256 -and $null -eq $c.AfterSha256)) { throw 'Invalid transaction plan.' }
        $seen[$c.Path] = $true
    }
    return $j
}
function Replace-ItemFile([string]$target, [string]$transaction, $change, [int]$index, [bool]$rollback) {
    $destination = Resolve-File $target $change.Path
    if ($rollback) { $expected = $change.AfterSha256; $desired = $change.BeforeSha256; $prefix = 'before-' } else { $expected = $change.BeforeSha256; $desired = $change.AfterSha256; $prefix = 'after-' }
    if ($rollback -and (File-Hash $destination) -ceq $desired) { return }
    Assert-Hash $destination $expected
    if ($null -eq $desired) { [IO.File]::Delete($destination); return }
    $source = Resolve-File $transaction ($prefix + $index.ToString('D4'))
    $parent = [IO.Path]::GetDirectoryName($destination)
    [void][IO.Directory]::CreateDirectory($parent)
    $temp = [IO.Path]::Combine($parent, '.dluma-transaction-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Checked $source $temp $desired
        Assert-Hash (Resolve-File $target $change.Path) $expected
        if ([IO.File]::Exists($destination)) {
            $displaced = $temp + '.old'
            try { [IO.File]::Replace($temp, $destination, $displaced) } finally { [IO.File]::Delete($displaced) }
        } else { [IO.File]::Move($temp, $destination) }
    } finally { [IO.File]::Delete($temp) }
}
function Recover([string]$transaction, [string]$target, [bool]$restore) {
    $j = Journal $transaction $target
    $committed = Resolve-File $transaction 'committed'
    if ($restore -and ![IO.File]::Exists($committed)) { throw 'Transaction has not completed.' }
    for ($i = 0; $i -lt @($j.Changes).Count; $i++) {
        $c = $j.Changes[$i]
        if ($null -ne $c.BeforeSha256) { Assert-Hash (Resolve-File $transaction ('before-' + $i.ToString('D4'))) $c.BeforeSha256 }
        $actual = File-Hash (Resolve-File $target $c.Path)
        if ($restore) { if ($actual -cne $c.AfterSha256) { throw 'Installed file has changed.' } }
        elseif ($actual -cne $c.BeforeSha256 -and $actual -cne $c.AfterSha256) { throw 'Transaction destination has changed.' }
    }
    if ($restore) { [IO.File]::Delete($committed) }
    for ($i = @($j.Changes).Count - 1; $i -ge 0; $i--) { Replace-ItemFile $target $transaction $j.Changes[$i] $i $true }
    Write-New (Resolve-File $transaction 'restored') '1'
}
function Get-Links([string]$root, [string]$target) {
    $links = @{}
    foreach ($file in [IO.Directory]::GetFiles($root, 'chain-*.json')) {
        $link = Read-Link $file
        if ($link.Target -ieq $target) { $links[$link.Id] = $link }
    }
    return $links
}
function Get-Head($links, [string]$root) {
    $active = @{}; $referenced = @{}
    foreach ($id in $links.Keys) { if ([IO.File]::Exists((Resolve-File $root ('transaction-' + $id + '/committed')))) { $active[$id] = $links[$id] } }
    foreach ($link in $active.Values) {
        if ($null -ne $link.Previous) {
            if (!$active.ContainsKey($link.Previous) -or $referenced.ContainsKey($link.Previous)) { throw 'Backup chain is incomplete or ambiguous.' }
            $referenced[$link.Previous] = $true
        }
    }
    $heads = @($active.Keys | Where-Object { !$referenced.ContainsKey($_) })
    if ($active.Count -eq 0) { return $null }
    if ($heads.Count -ne 1) { throw 'Backup chain is ambiguous.' }
    $seen = @{}; $id = $heads[0]
    while ($null -ne $id) { if ($seen.ContainsKey($id) -or !$active.ContainsKey($id)) { throw 'Backup chain is invalid.' }; $seen[$id] = $true; $id = $active[$id].Previous }
    if ($seen.Count -ne $active.Count) { throw 'Backup chain is disconnected.' }
    return $heads[0]
}
function Recover-Pending($links, [string]$root, [string]$target) {
    $recovered = $false
    foreach ($id in $links.Keys) {
        $tx = Resolve-File $root ('transaction-' + $id)
        if ([IO.File]::Exists((Resolve-File $tx 'journal.json')) -and ![IO.File]::Exists((Resolve-File $tx 'committed')) -and ![IO.File]::Exists((Resolve-File $tx 'restored'))) { Recover $tx $target $false; $recovered = $true }
    }
    return $recovered
}
function Extract-Package([string]$archive, [string]$destination, [string]$expectedHash) {
    Add-Type -AssemblyName System.IO.Compression
    $size = (Get-Item -LiteralPath $archive).Length
    if ($size -le 0 -or $size -gt 536870912) { throw 'Invalid archive size.' }
    Assert-Hash $archive $expectedHash.ToUpperInvariant()
    $stream = [IO.File]::Open($archive, 'Open', 'Read', 'Read')
    try {
        $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read, $false)
        try {
            if ($zip.Entries.Count -lt 1 -or $zip.Entries.Count -gt 2048) { throw 'Invalid archive entry count.' }
            $names = @{}; $mods = 0; [long]$total = 0
            $required = @('LICENSE','THIRD_PARTY_NOTICES.md','dluma.pak','dluma_lua54.dll','LICENSE_LUA_5.4.8.txt','dluma_discord_presence.dll','dluma_nightly_updater.exe')
            foreach ($entry in $zip.Entries) {
                $name = $entry.FullName
                [void](Resolve-File $destination $name)
                if ($name.EndsWith('/') -or @($name.Split('/') | Where-Object { $_ -ieq 'dluma.ini' }).Count -gt 0 -or $names.ContainsKey($name)) { throw 'Invalid archive path.' }
                $mode = ($entry.ExternalAttributes -shr 16) -band 0xF000
                if (($mode -ne 0 -and $mode -ne 0x8000) -or (($entry.ExternalAttributes -band 0x400) -ne 0)) { throw 'Archive contains a non-regular file.' }
                $names[$name] = $true
                $parts = $name.Split('/')
                if ($parts.Count -eq 1) { if ($name -match '^DLUMA \([0-9]+(?:\.[0-9]+)+\)\.mod$') { $mods++ } elseif ($required -inotcontains $name) { throw 'Unexpected package file.' } }
                elseif ($parts[0] -ine 'dluma_runtime') { throw 'Unexpected package directory.' }
                if ($entry.Length -lt 1 -or $entry.Length -gt (1073741824 - $total)) { throw 'Invalid extracted size.' }; $total += $entry.Length
            }
            if ($mods -ne 1) { throw 'Package requires one loader module.' }
            foreach ($name in $required) { if (!$names.ContainsKey($name)) { throw 'Package is incomplete.' } }
            [void][IO.Directory]::CreateDirectory($destination)
            foreach ($entry in $zip.Entries) {
                $output = Resolve-File $destination $entry.FullName
                [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output))
                $source = $entry.Open()
                try {
                    $sink = [IO.File]::Open($output, 'CreateNew', 'Write', 'None')
                    try {
                        $buffer = New-Object byte[] 65536; [long]$written = 0
                        while (($count = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                            $written += $count
                            if ($written -gt $entry.Length) { throw 'Archive entry exceeds its declared size.' }
                            $sink.Write($buffer, 0, $count)
                        }
                        $sink.Flush($true)
                    } finally { $sink.Dispose() }
                } finally { $source.Dispose() }
                if ((Get-Item -LiteralPath $output).Length -ne $entry.Length) { throw 'Archive entry is truncated.' }
            }
            $runtime = Runtime-Files $destination
            if (!$runtime.ContainsKey('dluma_runtime/updater/apply_nightly_update.ps1')) { throw 'Package updater is missing.' }
            foreach ($name in $names.Keys) { if ($name.StartsWith('dluma_runtime/', [StringComparison]::OrdinalIgnoreCase) -and !$runtime.ContainsKey($name)) { throw 'Unexpected runtime file.' } }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}

$resultFile = $null
$resultSuccess = $false
$resultCommit = ''
$failed = $false
try {
    $root = Full-Directory $PrivateRoot
    Check-Private $root
    if ($ResultPath) {
        $candidate = [IO.Path]::GetFullPath($ResultPath)
        if (!$candidate.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($candidate) -cnotmatch '^result-[0-9a-f]{64}\.ini$') { throw 'Invalid result path.' }
        $relative = $candidate.Substring($root.Length + 1).Replace('\', '/')
        $resultFile = Resolve-File $root $relative
        [void](Full-Directory ([IO.Path]::GetDirectoryName($resultFile)))
        if ([IO.Directory]::Exists($resultFile)) { throw 'Invalid result path.' }
    }
    if ($PlayerPid -le 0 -or $PlayerStartTicks -le 0) { throw 'Invalid process identity.' }
    $player = Get-PinnedPlayer $PlayerPid $PlayerStartTicks
    try {
        $module = [IO.Path]::GetFullPath($ModulePath)
        $target = Full-Directory ([IO.Path]::GetDirectoryName($module))
        if ([IO.Path]::GetFileName($module) -notmatch '^DLUMA \([0-9]+(?:\.[0-9]+)+\)\.mod$') { throw 'Invalid loader module.' }
        if ($target -ieq $root -or $target.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($target + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Updater and installation paths overlap.' }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $key = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($target.ToUpperInvariant())))).Replace('-', '') } finally { $sha.Dispose() }
        $mutex = New-Object Threading.Mutex($false, ('Global\DLUMA-Installer-' + $key))
        $held = $false
        try {
            try { $held = $mutex.WaitOne([TimeSpan]::FromSeconds(30)) } catch [Threading.AbandonedMutexException] { $held = $true }
            if (!$held) { throw 'Another updater is active.' }
            if ($null -ne $player) { $player.WaitForExit() }
            $links = Get-Links $root $target
            $recovered = Recover-Pending $links $root $target
            $head = Get-Head $links $root
            if ($Action -eq 'Restore') {
                if (!$head -and !$recovered) { throw 'No stable backup is available.' }
                while ($head) { $link = $links[$head]; Recover (Resolve-File $root ('transaction-' + $head)) $target $true; $head = $link.Previous }
                $resultSuccess = $true
                return
            }
            if ($ManifestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid manifest checksum.' }
            $stage = Full-Directory $StageDirectory
            if (!$stage.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Stage must be inside the private updater directory.' }
            $manifestPath = Resolve-File $stage 'nightly.json'
            Assert-Hash $manifestPath $ManifestSha256.ToUpperInvariant()
            $manifest = Read-Json $manifestPath 16384
            if (@($manifest.PSObject.Properties).Count -ne 4 -or $manifest.schema -cne 'dluma.nightly.v1' -or $manifest.commit -cnotmatch '^[0-9a-f]{40}$' -or $manifest.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $manifest.filename -cnotmatch '^DLUMA-[0-9]+(?:\.[0-9]+)+-([0-9a-f]{40})\.zip$' -or $Matches[1] -cne $manifest.commit) { throw 'Invalid nightly manifest.' }
            $resultCommit = $manifest.commit
            $id = [Guid]::NewGuid().ToString('N')
            $prepared = Resolve-File $root ('prepared-' + $id)
            try {
                Extract-Package (Resolve-File $stage 'archive.zip') $prepared $manifest.sha256
                $before = Runtime-Files $target; $after = Runtime-Files $prepared
                $rootOwned = @('dluma.pak','dluma_lua54.dll','LICENSE_LUA_5.4.8.txt','dluma_discord_presence.dll','dluma_nightly_updater.exe')
                $moduleName = [IO.Path]::GetFileName($module)
                if ($null -eq (File-Hash (Resolve-File $target $moduleName))) { throw 'Loader module is missing.' }
                foreach ($name in $rootOwned + @($moduleName)) { $hash = File-Hash (Resolve-File $target $name); if ($null -ne $hash) { $before[$name] = $hash } }
                $newModule = @([IO.Directory]::GetFiles($prepared, '*.mod') | ForEach-Object { [IO.Path]::GetFileName($_) })[0]
                foreach ($name in @('dluma.pak','dluma_lua54.dll','LICENSE_LUA_5.4.8.txt','dluma_discord_presence.dll','dluma_nightly_updater.exe', $newModule)) { $after[$name] = File-Hash (Resolve-File $prepared $name) }
                foreach ($name in @('LICENSE','THIRD_PARTY_NOTICES.md')) { if ($null -eq (File-Hash (Resolve-File $target $name))) { $after[$name] = File-Hash (Resolve-File $prepared $name) } }
                $all = @{}; foreach ($name in $before.Keys) { $all[$name] = $true }; foreach ($name in $after.Keys) { $all[$name] = $true }
                $changes = @()
                foreach ($name in @($all.Keys | Sort-Object)) {
                    $old = $before[$name]; $new = $after[$name]
                    if ($null -eq $old -and $null -ne (File-Hash (Resolve-File $target $name))) { throw 'Package file collides with an unowned file.' }
                    if ($old -cne $new) { $changes += [ordered]@{ Path=$name; BeforeSha256=$old; AfterSha256=$new } }
                }
                if ($changes.Count -eq 0) { $resultSuccess = $true; return }
                if ($changes.Count -gt 2048) { throw 'Transaction is too large.' }
                $tx = Resolve-File $root ('transaction-' + $id)
                Write-New (Resolve-File $root ('chain-' + $id + '.json')) (ConvertTo-Json -Compress ([ordered]@{ Version=1; Id=$id; Target=$target; Previous=$head; Commit=$manifest.commit }))
                [void][IO.Directory]::CreateDirectory($tx)
                for ($i = 0; $i -lt $changes.Count; $i++) {
                    $c = $changes[$i]; Assert-Hash (Resolve-File $target $c.Path) $c.BeforeSha256
                    if ($null -ne $c.BeforeSha256) { Copy-Checked (Resolve-File $target $c.Path) (Resolve-File $tx ('before-' + $i.ToString('D4'))) $c.BeforeSha256 }
                    if ($null -ne $c.AfterSha256) { Copy-Checked (Resolve-File $prepared $c.Path) (Resolve-File $tx ('after-' + $i.ToString('D4'))) $c.AfterSha256 }
                }
                Write-New (Resolve-File $tx 'journal.json') (ConvertTo-Json -Depth 5 -Compress ([ordered]@{ Version=1; Target=$target; Changes=@($changes) }))
                try {
                    for ($i = 0; $i -lt $changes.Count; $i++) { Replace-ItemFile $target $tx $changes[$i] $i $false }
                    Write-New (Resolve-File $tx 'committed') '1'
                    $resultSuccess = $true
                } catch { Recover $tx $target $false; throw }
            } finally { if ([IO.Directory]::Exists($prepared)) { [IO.Directory]::Delete($prepared, $true) } }
        } finally { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
    } finally { if ($null -ne $player) { $player.Dispose() } }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $failed = $true
} finally {
    if ($resultFile) {
        try {
            # Recheck every component immediately before creating the result.
            [void](Resolve-File $root $relative)
            Write-Result $resultFile $resultSuccess $Action $resultCommit
        } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
    }
}
if ($failed) { exit 1 }



