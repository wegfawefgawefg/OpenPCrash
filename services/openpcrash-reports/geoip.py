#!/usr/bin/env python3
"""Fetch DB-IP's free IP to City Lite database for the site's visit count (visits.py).

This month's file, or last month's when this month's isn't out yet, from download.db-ip.com (no
account), unpacked to DIR/geoip/dbip-city-lite.mmdb and checked (it opens and places a known
address) before it replaces the old one. The server picks the new file up within a minute.
openpcrash-geoip.timer runs this monthly, as root. DB-IP's data is CC BY 4.0: the site credits
it (https://db-ip.com).

    geoip.py [--dir /var/lib/openpcrash-reports]
"""
import argparse
import datetime
import gzip
import os
import shutil
import sys
import urllib.error
import urllib.request
from pathlib import Path

import maxminddb

URL = 'https://download.db-ip.com/free/dbip-city-lite-{month}.mmdb.gz'
NAME = 'dbip-city-lite.mmdb'


def months():
    today = datetime.datetime.now(datetime.timezone.utc).date()
    last = today.replace(day=1) - datetime.timedelta(days=1)
    return [today.strftime('%Y-%m'), last.strftime('%Y-%m')]


def fetch(url, into):
    request = urllib.request.Request(url, headers={'User-Agent': 'openpcrash-geoip'})
    with urllib.request.urlopen(request, timeout=60) as response, \
            gzip.GzipFile(fileobj=response) as unpacked, open(into, 'wb') as out:
        shutil.copyfileobj(unpacked, out, 1024 * 1024)


def usable(path):
    with maxminddb.open_database(str(path)) as reader:
        found = reader.get('1.1.1.1') or {}
        return 'City' in reader.metadata().database_type and 'country' in found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dir', default='/var/lib/openpcrash-reports')
    options = parser.parse_args()
    folder = Path(options.dir) / 'geoip'
    folder.mkdir(mode=0o755, exist_ok=True)
    partial = folder / (NAME + '.part')
    for month in months():
        url = URL.format(month=month)
        try:
            fetch(url, partial)
        except urllib.error.HTTPError as error:
            print(f'{url}: {error.code}')
            continue
        if not usable(partial):
            partial.unlink()
            sys.exit(f'{url}: not a city database; the old one stays')
        os.chmod(partial, 0o644)
        os.replace(partial, folder / NAME)
        print(f'{url}: {(folder / NAME).stat().st_size // (1024 * 1024)} MiB in {folder / NAME}')
        return
    sys.exit('no database this month or last; the old one stays')


if __name__ == '__main__':
    main()
