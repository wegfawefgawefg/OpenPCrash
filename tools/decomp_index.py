#!/usr/bin/env python3
"""The port's trail to the decomp: every `// decomp: name 0xADDR[, name 0xADDR...]` line in src/,
with the port function under it, as a table.

    tools/decomp_index.py                 port symbol, decomp name, address, file:line
    tools/decomp_index.py actor_step      rows whose port symbol, decomp name or address match
    tools/decomp_index.py 0x756F3         (an address matches with or without leading zeros)
    tools/decomp_index.py --missing       decomp: names the decomp's symbol tables don't have at
                                          that address (needs ../phantom-crash-decomp)
    tools/decomp_index.py --tsv           tab-separated, for other tools

Named constants carry their original value and decomp name in a comment beside the definition
(AGENTS.md "Style"), so `git grep` on the hex finds those; this index is for functions."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_decomp():
    # beside the repo; a worktree under .claude/worktrees looks further up
    at = ROOT
    while True:
        here = os.path.join(os.path.dirname(at), 'phantom-crash-decomp')
        if os.path.isdir(here) or os.path.dirname(at) == at:
            return here
        at = os.path.dirname(at)


DECOMP = os.environ.get('PHANTOMCRASH_DECOMP') or find_decomp()
TAG = re.compile(r'^\s*// decomp: (.*)$')
PAIR = re.compile(r'([A-Za-z_$?@][\w$?@]*(?:::~?\w+)?) 0x([0-9A-Fa-f]+)')
NAME = re.compile(r'([A-Za-z_]\w*)\s*\($')


def port_symbol(lines, i):
    """The function named by the signature after the tag line (the tag sits just above it, or
    above its template line)."""
    for j in range(i + 1, min(i + 4, len(lines))):
        line = lines[j]
        if line.startswith('template') or line.startswith('//'):
            continue
        code = line.split('//', 1)[0]
        p = code.find('(')
        if p < 0:
            return '?'
        m = NAME.search(code[:p + 1])
        return m.group(1) if m else '?'
    return '?'


def scan():
    rows = []
    src = os.path.join(ROOT, 'src')
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames.sort()
        for name in sorted(filenames):
            if not name.endswith(('.cpp', '.hpp')):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding='utf-8', errors='replace') as f:
                lines = f.read().split('\n')
            rel = os.path.relpath(path, ROOT)
            for i, line in enumerate(lines):
                m = TAG.match(line)
                if not m:
                    continue
                symbol = port_symbol(lines, i)
                for pair in PAIR.finditer(m.group(1)):
                    rows.append((symbol, pair.group(1), int(pair.group(2), 16), '%s:%d' % (rel, i + 1)))
    return rows


def decomp_symbols():
    names = {}
    data = os.path.join(DECOMP, 'data')
    if not os.path.isdir(data):
        return None
    for name in sorted(os.listdir(data)):
        if not (name.startswith('symbols') and name.endswith('.txt')):
            continue
        with open(os.path.join(data, name)) as f:
            for line in f:
                m = re.match(r'\s*([\w$?@]+(?:::~?\w+)?)\s*=\s*0x([0-9A-Fa-f]+)', line)
                if m:
                    names.setdefault(m.group(1), int(m.group(2), 16))
    return names


def main():
    args = sys.argv[1:]
    tsv = '--tsv' in args
    missing = '--missing' in args
    args = [a for a in args if not a.startswith('--')]
    rows = scan()
    if missing:
        names = decomp_symbols()
        if names is None:
            print('no decomp at %s' % DECOMP, file=sys.stderr)
            return 1
        rows = [r for r in rows if names.get(r[1]) != r[2]]
    if args:
        want = args[0]
        addr = int(want, 16) if re.fullmatch(r'0[xX][0-9A-Fa-f]+', want) else None
        rows = [r for r in rows if r[2] == addr or want in (r[0], r[1]) or
                (addr is None and (want in r[0] or want in r[1]))]
    width = max([len(r[0]) for r in rows] + [4])
    dwidth = max([len(r[1]) for r in rows] + [6])
    for symbol, name, addr, where in rows:
        if tsv:
            print('%s\t%s\t0x%X\t%s' % (symbol, name, addr, where))
        else:
            print('%-*s  %-*s  0x%-6X  %s' % (width, symbol, dwidth, name, addr, where))
    if not tsv and not args and not missing:
        print('%d tags, %d port functions' % (len(rows), len({r[3] for r in rows})), file=sys.stderr)
    return 0


sys.exit(main())
