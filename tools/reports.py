#!/usr/bin/env python3
"""Read the bug reports players' games sent (docs/release.md, "Bug reports"), on the owner's machine.

    tools/reports.py pull                    copy them off the server into bug-reports/
    tools/reports.py [list] [--kind KIND] [--last N]
                                             one line each, newest last
    tools/reports.py show ID [--log N] [--exe build/phantomcrash]
                                             one in full (an ID's first few digits do), the
                                             game's own stack frames named by addr2line from
                                             a build of the same commit (its .debug file does)
    tools/reports.py replay ID [--hash N] [--sim build/pc_sim] [--cooked DIR] [--data DIR]
                                             its fight stepped again by pc_sim --replay FILE

The server is reached over ssh: ~/.config/openpcrash/report_ssh holds "user@host" (and, after a
space, the key to use); without it, root@ the host of ~/.config/openpcrash/report_url. Neither
file is in the repository. A local folder of reports (--from DIR, as server.py --dir writes
them) reads the same way.
"""
import argparse
import datetime
import json
import shlex
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / 'bug-reports' / 'server'
CONFIG = Path.home() / '.config' / 'openpcrash'
REMOTE = '/var/lib/openpcrash-reports/'


def ssh_target():
    """The ssh destination and its options, from the owner's own config."""
    ssh = CONFIG / 'report_ssh'
    if ssh.exists():
        parts = ssh.read_text().split()
        options = ['-o', 'BatchMode=yes']
        if len(parts) > 1:
            options += ['-i', str(Path(parts[1]).expanduser())]
        return parts[0], options
    url = CONFIG / 'report_url'
    if url.exists():
        return 'root@' + urlparse(url.read_text().strip()).hostname, ['-o', 'BatchMode=yes']
    sys.exit(f'no server: write {ssh} ("user@host [key]") or {url}')


def pull():
    target, options = ssh_target()
    LOCAL.mkdir(parents=True, exist_ok=True)
    shell = 'ssh ' + ' '.join(shlex.quote(o) for o in options)
    subprocess.run(['rsync', '-az', '-e', shell, f'{target}:{REMOTE}', str(LOCAL) + '/'], check=True)
    print(f'{len(load(LOCAL))} reports in {LOCAL.relative_to(ROOT)}')


def load(folder):
    reports = []
    logs = sorted(folder.glob('reports.jsonl*'), key=lambda path: (len(path.suffixes), path.name),
                  reverse=True)
    for path in logs:
        for line in path.read_text(errors='replace').splitlines():
            try:
                reports.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    reports.sort(key=lambda report: report.get('received', 0))
    return reports


def when(report):
    return datetime.datetime.fromtimestamp(report.get('received', 0)).strftime('%Y-%m-%d %H:%M')


def one_line(report):
    build = report.get('build', {})
    system = report.get('system', {})
    extras = ''.join(f' [{part}]' for part in ('shot', 'replay') if report.get(part))
    what = report.get('note') or report.get('message', '')
    return (f"{report['id'][:8]} {when(report)} {report.get('kind', '?'):8} "
            f"{build.get('version', '?')} {build.get('commit') or 'dev'} "
            f"{system.get('platform', '?')}{extras}: {what[:70]}")


def find(reports, wanted):
    found = [report for report in reports if report.get('id', '').startswith(wanted)]
    if len(found) != 1:
        sys.exit(f'{len(found)} reports start {wanted}')
    return found[0]


def named(line, exe):
    """A stack line "0x... phantomcrash+0x1f3a" with its function and line, when exe is given."""
    if not exe or '+0x' not in line:
        return line
    module, offset = line.split()[-1].rsplit('+', 1)
    if module.lower().removesuffix('.exe') not in ('phantomcrash', 'openpcrash'):
        return line
    # a return address names the line after its call: one byte back is the call
    at = int(offset, 16) - (0 if line.startswith('pc ') else 1)
    found = subprocess.run(['addr2line', '-f', '-C', '-e', str(exe), hex(at)], capture_output=True,
                           text=True).stdout.split('\n')
    return f'{line}  {found[0]} {Path(found[1]).name}' if len(found) > 1 else line


def show(report, log_lines, folder, exe=None):
    for key in ('id', 'kind', 'session', 'uptime', 'message', 'note'):
        if key in report:
            print(f'{key}: {report[key]}')
    print('received:', when(report))
    for part in ('build', 'system', 'game'):
        print(f'{part}:', json.dumps(report.get(part, {})))
    print('settings:', ', '.join(f'{k}={v}' for k, v in report.get('settings', {}).items()))
    for part, sub in (('shot', 'shots'), ('replay', 'replays')):
        if report.get(part):
            print(f'{part}:', folder / sub / report[part])
    if report.get('stack'):
        print('stack:')
        for line in report['stack']:
            print('  ' + named(line, exe))
    print(f'log (last {log_lines} lines):')
    for line in report.get('log', [])[-log_lines:]:
        print('  ' + line)


def replay(report, options, folder):
    if not report.get('replay'):
        sys.exit('this report carries no fight')
    record = folder / 'replays' / report['replay']
    build = report.get('build', {})
    print(f"the report's build: {build.get('label', '?')} (commit {build.get('commit') or 'dev'}); "
          'replay it on a build of that commit')
    command = [str(options.sim), '--replay', str(record), '--hash', str(options.hash)]
    if options.cooked:
        command += ['--cooked', str(options.cooked)]
    if options.data:
        command += ['--data', str(options.data)]
    print('$ ' + ' '.join(shlex.quote(part) for part in command))
    sys.exit(subprocess.run(command).returncode)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', nargs='?', default='list',
                        choices=['pull', 'list', 'show', 'replay'])
    parser.add_argument('id', nargs='?')
    parser.add_argument('--from', dest='folder', type=Path, default=LOCAL)
    parser.add_argument('--kind')
    parser.add_argument('--last', type=int, default=40)
    parser.add_argument('--log', type=int, default=60)
    parser.add_argument('--hash', type=int, default=0)
    parser.add_argument('--sim', type=Path, default=ROOT / 'build' / 'pc_sim')
    parser.add_argument('--cooked', type=Path)
    parser.add_argument('--data', type=Path)
    parser.add_argument('--exe', type=Path)
    options = parser.parse_args()
    if options.command == 'pull':
        pull()
        return
    reports = load(options.folder)
    if options.command == 'list':
        chosen = [r for r in reports if not options.kind or r.get('kind') == options.kind]
        for report in chosen[-options.last:]:
            print(one_line(report))
        return
    if not options.id:
        sys.exit(f'{options.command} needs a report ID')
    report = find(reports, options.id)
    if options.command == 'show':
        show(report, options.log, options.folder, options.exe)
    else:
        replay(report, options, options.folder)


if __name__ == '__main__':
    main()
