import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'apply_nightly_update.ps1'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def package(root, version):
    runtime = {'engine.bin': ('runtime-' + version).encode(), 'updater/apply_nightly_update.ps1': SCRIPT.read_bytes()}
    manifest = {'schema': 'dluma.runtime-tree.v1', 'files': [
        {'path': name, 'sha256': digest(data).upper(), 'size': len(data)}
        for name, data in runtime.items()]}
    files = {
        'LICENSE': b'license', 'THIRD_PARTY_NOTICES.md': b'notices',
        'LICENSE_LUA_5.4.8.txt': b'lua license', 'dluma.pak': ('pak-' + version).encode(),
        'dluma_lua54.dll': b'lua', 'dluma_discord_presence.dll': b'discord',
        'dluma_nightly_updater.exe': ('launcher-' + version).encode(),
        f'DLUMA ({version}).mod': ('module-' + version).encode(),
        'dluma_runtime/runtime-manifest.json': json.dumps(manifest).encode(),
    }
    files.update({'dluma_runtime/' + k: v for k, v in runtime.items()})
    root.mkdir(parents=True)
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return files


def invoke(module, private, stage=None, manifest_hash='', action='Update', wrong_ticks=False, exited_before_start=False):
    args = [str(SCRIPT), str(module), str(private), str(stage or ''), manifest_hash, action, str(wrong_ticks), str(private / ('result-' + 'b' * 64 + '.ini')), os.environ.get('DLUMA_TEST_NIGHTLY_LAUNCHER', ''), str(exited_before_start)]
    command = r'''param($script,$module,$private,$stage,$hash,$action,$wrong,$result,$launcher,$exited)
$p = Start-Process -FilePath $env:ComSpec -ArgumentList '/c ping -n 3 127.0.0.1 >nul' -PassThru -WindowStyle Hidden
try {
  $ticks = $p.StartTime.ToUniversalTime().Ticks
  if ($wrong -eq 'True') { $ticks++ }
  if ($exited -eq 'True') { Stop-Process -Id $p.Id -Force; $p.WaitForExit() }
  if ($launcher) {
    $arguments = @('-File', $script, '-ModulePath', $module, '-PrivateRoot', $private, '-Action', $action, '-PlayerPid', $p.Id, '-PlayerStartTicks', $ticks, '-ResultPath', $result)
    if ($action -eq 'Update') { $arguments += @('-StageDirectory', $stage, '-ManifestSha256', $hash) }
    & $launcher @arguments
  } else {
    & $script -ModulePath $module -PrivateRoot $private -StageDirectory $stage -ManifestSha256 $hash -Action $action -PlayerPid $p.Id -PlayerStartTicks $ticks -ResultPath $result
  }
  exit $LASTEXITCODE
} finally { if (!$p.HasExited) { Stop-Process -Id $p.Id -Force } }
'''
    with tempfile.NamedTemporaryFile(mode='w', suffix='.ps1', delete=False) as f:
        f.write(command)
        runner = f.name
    try:
        return subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File', runner, *args], capture_output=True, text=True)
    finally:
        os.unlink(runner)


def main():
    with tempfile.TemporaryDirectory(prefix='dluma-apply-') as temp:
        base = Path(temp)
        private, target, staging, source = (base / x for x in ('private updater', 'target with spaces', 'private updater/stage with spaces', 'source'))
        private.mkdir()
        # Match the updater's current-user-only private directory contract.
        acl = r'''param($p) $id=[Security.Principal.WindowsIdentity]::GetCurrent().User; $a=New-Object Security.AccessControl.DirectorySecurity; $a.SetOwner($id); $a.SetAccessRuleProtection($true,$false); $r=New-Object Security.AccessControl.FileSystemAccessRule($id,'FullControl','ContainerInherit,ObjectInherit','None','Allow'); $a.AddAccessRule($r); (New-Object IO.DirectoryInfo($p)).SetAccessControl($a)'''
        acl_script = base / 'acl.ps1'
        acl_script.write_text(acl)
        subprocess.run(['powershell.exe', '-NoProfile', '-File', str(acl_script), str(private)], check=True)
        old = package(target, '1.0')
        new = package(source, '2.0')
        (target / 'dluma_runtime' / 'dluma.ini').write_text('settings')
        (target / 'other.mod').write_text('other')
        staging.mkdir()
        archive = staging / 'archive.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in new.items():
                z.writestr(name, data)
        commit = 'a' * 40
        raw = json.dumps({'schema': 'dluma.nightly.v1', 'commit': commit, 'filename': f'DLUMA-2.0-{commit}.zip', 'sha256': digest(archive.read_bytes())}).encode()
        (staging / 'nightly.json').write_bytes(raw)
        module = target / 'DLUMA (1.0).mod'
        result_path = private / ('result-' + 'b' * 64 + '.ini')
        bad = invoke(module, private, staging, digest(raw), wrong_ticks=True)
        assert bad.returncode != 0 and module.read_bytes() == old[module.name], (bad.stdout, bad.stderr)
        assert 'success=0' in result_path.read_text(encoding='utf-16')
        bad = invoke(module, private, staging, '0' * 64)
        assert bad.returncode != 0 and module.read_bytes() == old[module.name], (bad.stdout, bad.stderr)
        result = invoke(module, private, staging, digest(raw), exited_before_start=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        report = result_path.read_text(encoding='utf-16')
        assert 'success=1' in report and 'action=Update' in report and f'commit={commit}' in report
        assert (target / 'DLUMA (2.0).mod').read_bytes() == new['DLUMA (2.0).mod']
        assert not module.exists()
        assert (target / 'dluma_runtime' / 'dluma.ini').read_text() == 'settings'
        assert (target / 'other.mod').read_text() == 'other'
        result = invoke(target / 'DLUMA (2.0).mod', private, action='Restore', exited_before_start=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        report = result_path.read_text(encoding='utf-16')
        assert 'success=1' in report and 'action=Restore' in report
        assert module.read_bytes() == old[module.name]
        assert not (target / 'DLUMA (2.0).mod').exists()
        # A corrupt archive must be rejected before replacement.
        archive.write_bytes(archive.read_bytes() + b'corrupt')
        result = invoke(module, private, staging, digest(raw))
        assert result.returncode != 0 and module.read_bytes() == old[module.name]
        report = result_path.read_text(encoding='utf-16')
        assert 'success=0' in report and 'action=Update' in report and f'commit={commit}' in report
        archive.write_bytes(archive.read_bytes()[:-7])
        # An interrupted committed transaction is rolled back on the next restore.
        result = invoke(module, private, staging, digest(raw))
        assert result.returncode == 0, (result.stdout, result.stderr)
        transactions = [p for p in private.glob('transaction-*') if (p / 'committed').exists()]
        assert len(transactions) == 1
        transaction = transactions[0]
        journal = json.loads((transaction / 'journal.json').read_text())
        # Simulate termination after only some replacements took place.
        for index, change in enumerate(journal['Changes']):
            if change['BeforeSha256'] and change['AfterSha256']:
                (target / change['Path']).write_bytes((transaction / f'before-{index:04d}').read_bytes())
                break
        else:
            raise AssertionError('fixture needs a replaced file')
        (transaction / 'committed').unlink()
        result = invoke(target / 'DLUMA (2.0).mod', private, action='Restore')
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert module.read_bytes() == old[module.name]
        assert not (target / 'DLUMA (2.0).mod').exists()
        print('Installer core checks passed')


if __name__ == '__main__':
    main()

