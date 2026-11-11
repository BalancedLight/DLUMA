"""Switch a disposable project's single generated quest; compile it again in the Editor."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from scenario import SCENARIOS, render

def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--project',type=Path,required=True);a.add_argument('--map',required=True)
    a.add_argument('--scenario',choices=tuple(SCENARIOS),required=True);a.add_argument('--backup-dir',type=Path,required=True)
    a.add_argument('--confirm-disposable',action='store_true');args=a.parse_args()
    if not args.confirm_disposable:raise ValueError('Confirm the project is disposable')
    project=args.project.resolve(strict=True);backup=args.backup_dir.resolve()
    if not (project/'desc.scr').is_file() or backup.exists() or backup==project or project in backup.parents or backup in project.parents:
        raise ValueError('Project or new external backup directory invalid')
    render(args.map,args.scenario)
    directory=project/'data/quests'/args.map
    for p in (project,project/'data',project/'data/quests',directory):
        st=p.lstat()
        if p.is_symlink() or getattr(st,'st_file_attributes',0)&0x400:raise ValueError('Reparse point rejected: '+str(p))
    files=[directory/'dluma_boss_probe.pxsl',directory/(args.map+'_invoke.scr')]
    old=[]
    for p in files:
        st=p.lstat()
        if not p.is_file() or p.is_symlink() or getattr(st,'st_file_attributes',0)&0x400:raise ValueError('Invalid quest input')
        old.append(p.read_bytes())
    normalized = tuple(x.replace(b'\r\n', b'\n') for x in old)
    generated = [tuple(x.encode('utf-8') for x in render(args.map, mode)) for mode in SCENARIOS]
    known_pair = normalized in generated
    known_f7_header = (any(normalized[0] == pair[0] for pair in generated) and
                       normalized[1].rstrip(b'\n') == b'!QuestStage( s, s, s )')
    if not known_pair and not known_f7_header:
        raise ValueError('Existing quest differs from a generated scenario; refusing overwrite')
    new=tuple(x.encode('utf-8') for x in render(args.map,args.scenario))
    backup.mkdir(parents=True)
    for p,data in zip(files,old): (backup/p.name).write_bytes(data)
    (backup/'journal.json').write_text(json.dumps({'project':str(project),'scenario':args.scenario,'original_sha256':[hashlib.sha256(x).hexdigest() for x in old]},indent=2)+'\n')
    for p,before,after in zip(files,old,new):
        if p.read_bytes()!=before:raise ValueError('Quest changed during selection; backup retained')
        temp=p.with_name(p.name+'.dluma-next')
        with temp.open('xb') as f:f.write(after);f.flush();os.fsync(f.fileno())
        os.replace(temp,p)
    print('Selected',args.scenario,'; compile the PXSL with Editor F7 before play. Backup:',backup)

if __name__=='__main__':
    try:main()
    except (OSError,ValueError) as e:print('Scenario selection failed:',e,file=sys.stderr);sys.exit(1)
