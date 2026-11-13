#!/usr/bin/env python3
"""Who visits the site (docs/release.md, "Site visits"): the page's own count, and GitHub's numbers.

    tools/site_stats.py [--days N] [--top N] [--from DIR] [--no-github]

The page's count is the server's visits-YYYY-MM.jsonl (services/openpcrash-reports/visits.py),
read over ssh as tools/reports.py reaches the server (~/.config/openpcrash/report_ssh); --from
reads a local folder of them instead. Days are UTC. A visitor is one browser on one day (the
server's daily hash), so a week's visitors are the sum of its days'. GitHub's release download
counts and repo traffic (views and referrers, its last 14 days) come from `gh api`, when gh is
there and logged in.
"""
import argparse
import collections
import datetime
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from reports import REMOTE, ssh_target

REPO = 'wegfawefgawefg/OpenPCrash'
SYSTEMS = ('windows', 'mac', 'linux')


def months(first, last):
    month, names = first.replace(day=1), []
    while month <= last:
        names.append(month.strftime('visits-%Y-%m.jsonl'))
        month = (month + datetime.timedelta(days=32)).replace(day=1)
    return names


def read(names, folder):
    if folder:
        return '\n'.join((folder / name).read_text() for name in names if (folder / name).exists())
    target, options = ssh_target()
    files = ' '.join(shlex.quote(name) for name in names)
    command = f'cd {REMOTE} && for f in {files}; do [ -e "$f" ] && cat "$f"; done; true'
    done = subprocess.run(['ssh', *options, target, command], capture_output=True, text=True)
    if done.returncode:
        sys.exit(f'ssh {target}: {done.stderr.strip()}')
    return done.stdout


def day_of(hit):
    return datetime.datetime.fromtimestamp(hit['time'], datetime.timezone.utc).date()


def load(text, first):
    hits = []
    for line in text.splitlines():
        try:
            hit = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(hit, dict) and isinstance(hit.get('time'), int) and day_of(hit) >= first:
            hits.append(hit)
    return hits


def visitor(hit):
    return day_of(hit), hit.get('visitor')


def table(title, rows, top):
    print(f'\n{title:34.34} {"views":>7} {"visitors":>8}')
    if not rows:
        print('  none')
    for name, views, visitors in rows[:top]:
        print(f'  {name:32.32} {views:7} {visitors:8}')
    if len(rows) > top:
        print(f'  ... {len(rows) - top} more')


def tally(hits, key):
    """(name, views, visitors) for one field over the views, most visitors first."""
    views, visitors = collections.Counter(), collections.defaultdict(set)
    for hit in hits:
        if hit.get('kind') == 'view':
            name = key(hit)
            views[name] += 1
            visitors[name].add(visitor(hit))
    rows = [(name, views[name], len(visitors[name])) for name in views]
    return sorted(rows, key=lambda row: (-row[2], -row[1], row[0]))


def page_count(hits, first, last, top):
    print(f'The site, {first} to {last} (UTC)')
    print('\nViews, visitors, and clicks on the downloads and the source link, a day')
    print(f'  {"day":10} {"views":>7} {"visitors":>8} ' +
          ' '.join(f'{name:>7}' for name in SYSTEMS) + f' {"source":>7}')
    by_day = collections.defaultdict(list)
    for hit in hits:
        by_day[day_of(hit)].append(hit)

    def row(name, counts):
        views, visitors, *clicks = counts
        print(f'  {name:10} {views:>7} {visitors:>8} ' + ' '.join(f'{n:>7}' for n in clicks))

    totals = [0] * 6
    day = first
    while day <= last:
        kinds = collections.Counter(hit.get('kind') for hit in by_day[day])
        seen = len({hit.get('visitor') for hit in by_day[day] if hit.get('kind') == 'view'})
        counts = [kinds['view'], seen] + [kinds['download-' + name] for name in SYSTEMS] + \
            [kinds['source']]
        totals = [a + b for a, b in zip(totals, counts)]
        if any(counts):
            row(day.isoformat(), counts)
        day += datetime.timedelta(days=1)
    row('total', totals)
    table('Countries', tally(hits, lambda hit: hit.get('country', '?')), top)
    table('Cities', tally(hits, lambda hit: f"{hit.get('city', '?')}, {hit.get('country', '?')}"),
          top)
    table('Referring sites', tally(hits, lambda hit: hit.get('referrer', '(none)')), top)
    table('Sources (?ref= or ?utm_source=)', tally(hits, lambda hit: hit.get('source', '(none)')),
          top)
    table('Browsers', tally(hits, lambda hit: hit.get('browser', '?')), top)
    table('Systems', tally(hits, lambda hit: hit.get('os', '?')), top)
    table('Screens', tally(hits, lambda hit: hit.get('screen', '?')), top)
    table('Languages', tally(hits, lambda hit: hit.get('lang', '?')), top)

    print(f'\n{"Clicks":34} {"clicks":>7} {"visitors":>8}')
    for kind in [f'download-{name}' for name in SYSTEMS] + ['source']:
        chosen = [hit for hit in hits if hit.get('kind') == kind]
        print(f'  {kind:32} {len(chosen):>7} {len({visitor(hit) for hit in chosen}):>8}')


def gh(path):
    done = subprocess.run(['gh', 'api', path], capture_output=True, text=True)
    if done.returncode:
        raise RuntimeError(done.stderr.strip().splitlines()[-1] if done.stderr.strip() else 'failed')
    return json.loads(done.stdout)


def github(top):
    print(f'\nGitHub ({REPO})')
    if not shutil.which('gh'):
        print('  no gh here')
        return
    try:
        releases = gh(f'repos/{REPO}/releases')
        print('\n  Release downloads (all time)')
        for release in releases:
            kind = ' pre-release' if release.get('prerelease') else ''
            print(f"  {release['tag_name']}{kind}, {release.get('published_at', '')[:10]}")
            for asset in release.get('assets', []):
                print(f"    {asset['name']:36} {asset['download_count']:>7}")
    except RuntimeError as error:
        print('  releases:', error)
    try:
        views = gh(f'repos/{REPO}/traffic/views')
        print(f"\n  Repo page views, last 14 days: {views['count']} views, {views['uniques']} unique")
        for day in [day for day in views.get('views', []) if day['count']]:
            print(f"    {day['timestamp'][:10]} {day['count']:>7} {day['uniques']:>8}")
        referrers = gh(f'repos/{REPO}/traffic/popular/referrers')
        print('\n  Repo referrers, last 14 days (views, unique)')
        for row in referrers[:top] or [{'referrer': 'none', 'count': 0, 'uniques': 0}]:
            print(f"    {row['referrer']:32.32} {row['count']:>7} {row['uniques']:>8}")
    except RuntimeError as error:
        print('  traffic:', error)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--days', type=int, default=30)
    parser.add_argument('--top', type=int, default=12)
    parser.add_argument('--from', dest='folder', type=Path,
                        help='a local folder of visits-YYYY-MM.jsonl instead of the server')
    parser.add_argument('--no-github', action='store_true')
    options = parser.parse_args()
    last = datetime.datetime.now(datetime.timezone.utc).date()
    first = last - datetime.timedelta(days=max(1, options.days) - 1)
    hits = load(read(months(first, last), options.folder), first)
    page_count(hits, first, last, options.top)
    if not options.no_github:
        github(options.top)


if __name__ == '__main__':
    main()
