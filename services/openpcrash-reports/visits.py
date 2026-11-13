"""The site's own visit count: POST /openpcrash-reports/visit, from the beacon in site/index.html.

One JSON line a page view or a download or source click, in visits-YYYY-MM.jsonl. The address is
never written: it gives a country and city from DB-IP's free database (geoip.py keeps it fresh)
and, with the User-Agent and a salt that lives in memory for one UTC day, a visitor hash that
only says which of a day's hits came from the same browser. The User-Agent is kept only as a
browser and a system name. Bots are dropped. See docs/release.md ("Site visits").
"""
import datetime
import hashlib
import json
import os
import re
import secrets
import time
from pathlib import Path

from aiohttp import web

try:
    import maxminddb
except ImportError:
    maxminddb = None

ORIGINS = {'https://wegfawefgawefg.github.io'}
KINDS = {'view', 'download-windows', 'download-mac', 'download-linux', 'source'}
SCREENS = {'phone', 'tablet', 'laptop', 'desktop', 'wide'}
BODY_LIMIT = 2048
PER_IP_HOUR = 60
ALL_TEN_MINUTES = 6000
PEERS_MOST = 100000              # addresses held for the hour's limit
MONTH_LIMIT = 256 * 1024 * 1024  # a month's file stops growing here
GEOIP = 'geoip/dbip-city-lite.mmdb'

PATH = re.compile(r'/[A-Za-z0-9/._~%-]{0,199}')
HOST = re.compile(r'[a-z0-9]([a-z0-9-]{0,62}\.){0,8}[a-z0-9-]{1,63}')
SOURCE = re.compile(r'[A-Za-z0-9._-]{1,64}')
LANG = re.compile(r'[A-Za-z]{2,3}(-[A-Za-z0-9]{1,8}){0,3}')
BOTS = re.compile(r'bot|crawl|spider|slurp|headless|lighthouse|preview|curl|wget|python|go-http|'
                  r'okhttp|java/|scrapy|phantomjs|puppeteer|playwright|selenium', re.IGNORECASE)

BROWSERS = (('Edge', re.compile(r'Edg/|EdgA/|EdgiOS/')), ('Opera', re.compile(r'OPR/|Opera')),
            ('Samsung', re.compile(r'SamsungBrowser/')), ('Firefox', re.compile(r'Firefox/|FxiOS/')),
            ('Chrome', re.compile(r'Chrome/|CriOS/|Chromium/')), ('Safari', re.compile(r'Safari/')))
SYSTEMS = (('Windows', re.compile(r'Windows')), ('Android', re.compile(r'Android')),
           ('iOS', re.compile(r'iPhone|iPad|iPod')), ('ChromeOS', re.compile(r'CrOS')),
           ('macOS', re.compile(r'Macintosh|Mac OS X')), ('Linux', re.compile(r'Linux|X11')))


def family(agent, families):
    for name, pattern in families:
        if pattern.search(agent):
            return name
    return 'other'


def fields(data):
    """The hit's own fields as kept, or None when it isn't one. A field that doesn't fit is left
    out rather than losing the view."""
    if not isinstance(data, dict) or data.get('kind') not in KINDS:
        return None
    kept = {'kind': data['kind']}
    for key, pattern in (('path', PATH), ('referrer', HOST), ('source', SOURCE), ('lang', LANG)):
        value = data.get(key)
        if isinstance(value, str) and pattern.fullmatch(value):
            kept[key] = value
    if data.get('screen') in SCREENS:
        kept['screen'] = data['screen']
    return kept


class Limits:
    """Fixed windows: an address an hour, everyone ten minutes. Addresses only ever in memory."""

    def __init__(self):
        self.hour, self.peers = None, {}
        self.slot, self.count = None, 0

    def allow(self, address, now):
        if int(now // 3600) != self.hour:
            self.hour, self.peers = int(now // 3600), {}
        if int(now // 600) != self.slot:
            self.slot, self.count = int(now // 600), 0
        seen = self.peers.get(address, 0)
        if seen >= PER_IP_HOUR or self.count >= ALL_TEN_MINUTES or \
                (not seen and len(self.peers) >= PEERS_MOST):
            return False
        self.peers[address] = seen + 1
        self.count += 1
        return True


class Visitors:
    """A visitor hash per UTC day: the day's salt is random, in memory, gone when the day ends (a
    restart starts a new one, so that day's same browser counts twice)."""

    def __init__(self):
        self.day, self.salt = None, b''

    def hash(self, day, address, agent):
        if day != self.day:
            self.day, self.salt = day, secrets.token_bytes(32)
        mixed = self.salt + address.encode() + b'\0' + agent.encode(errors='replace')
        return hashlib.sha256(mixed).hexdigest()[:16]


class Geo:
    """Country and city of an address from the DB-IP database, reopened when geoip.py replaces
    it; nothing when it's missing."""

    def __init__(self, path):
        self.path, self.reader, self.stamp, self.checked = path, None, None, -1e9

    def lookup(self, address, now):
        if now - self.checked > 60:
            self.checked = now
            try:
                stamp = self.path.stat().st_mtime
            except OSError:
                stamp = None
            if stamp != self.stamp and maxminddb:
                self.close()
                self.stamp = stamp
                if stamp is not None:
                    self.reader = maxminddb.open_database(str(self.path))
        if not self.reader:
            return {}
        try:
            found = self.reader.get(address) or {}
        except ValueError:
            return {}
        place = {}
        country = found.get('country', {}).get('iso_code')
        city = found.get('city', {}).get('names', {}).get('en')
        if isinstance(country, str):
            place['country'] = country
        if isinstance(city, str):
            place['city'] = city[:80]
        return place

    def close(self):
        if self.reader:
            self.reader.close()
            self.reader = None


def write(directory, record, now):
    path = directory / time.strftime('visits-%Y-%m.jsonl', time.gmtime(now))
    try:
        if path.stat().st_size > MONTH_LIMIT:
            return
    except FileNotFoundError:
        pass
    line = (json.dumps(record, separators=(',', ':'), ensure_ascii=False) + '\n').encode()
    descriptor = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
    try:
        os.write(descriptor, line)
    finally:
        os.close(descriptor)


def add_routes(app, directory):
    directory = Path(directory)
    limits, visitors, geo = Limits(), Visitors(), Geo(directory / GEOIP)

    def answer(status, origin):
        return web.Response(status=status, headers={'Access-Control-Allow-Origin': origin,
                                                    'Vary': 'Origin'})

    async def visit(request):
        origin = request.headers.get('Origin')
        if origin not in ORIGINS:
            return web.Response(status=403)
        agent = request.headers.get('User-Agent', '')
        if not agent or BOTS.search(agent):
            return answer(204, origin)
        address = request.headers.get('X-Real-IP', request.remote) or ''
        now = time.time()
        if not limits.allow(address, time.monotonic()):
            return answer(429, origin)
        body = await request.read()
        if len(body) > BODY_LIMIT:
            return answer(413, origin)
        try:
            hit = fields(json.loads(body))
        except (ValueError, RecursionError):
            hit = None
        if hit is None:
            return answer(400, origin)
        day = datetime.datetime.fromtimestamp(now, datetime.timezone.utc).date().isoformat()
        record = {'time': int(now), **hit, **geo.lookup(address, time.monotonic()),
                  'browser': family(agent, BROWSERS), 'os': family(agent, SYSTEMS),
                  'visitor': visitors.hash(day, address, agent)}
        write(directory, record, now)
        return answer(204, origin)

    async def close(app):
        geo.close()

    app.router.add_post('/openpcrash-reports/visit', visit)
    app.on_cleanup.append(close)
