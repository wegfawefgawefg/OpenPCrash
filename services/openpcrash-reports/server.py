#!/usr/bin/env python3
"""OpenPCrash bug reports: taken in, checked, kept. No way to read them back over HTTP.

POST /openpcrash-reports, behind nginx's `location = /openpcrash-reports` (install.py). The
game sends the X-OpenPCrash-Token header (a static token that only keeps junk out). Each report
is one JSON line in reports.jsonl (rotated); its picture, if any, a file in shots/; its fight
replay, if any, a file in replays/ (tools/reports.py replays it). Addresses are used only for
the rate limits, in memory, and never stored. See docs/release.md ("Bug reports").

POST /openpcrash-reports/visit is the site's visit count (visits.py).

    server.py [--dir DIR] [--host 127.0.0.1] [--port 8796]
"""
import argparse
import base64
import collections
import json
import logging
import re
import time
from logging.handlers import RotatingFileHandler
from pathlib import Path

from aiohttp import web

import visits

TOKEN = 'openpcrash-reports-1'
KINDS = {'crash', 'hang', 'renderer', 'startup', 'manual'}
JSON_LIMIT = 256 * 1024          # a report without its picture and replay
SHOT_LIMIT = 400 * 1024          # the picture, decoded
REPLAY_LIMIT = 1536 * 1024       # the fight's record, packed
BODY_LIMIT = 3 * 1024 * 1024     # the whole request
PER_IP_HOUR = 20
ALL_TEN_MINUTES = 200
FILES_KEPT = 1000                # pictures, and replays, the oldest dropped

HEX16 = re.compile(r'[0-9a-f]{16}')
STAMP = re.compile(r'[A-Za-z0-9 ._+-]{0,64}')
NAME = re.compile(r'[a-z][a-z0-9_]{0,31}')
SETTING = re.compile(r'[a-z0-9_.]{1,64}')


class Invalid(ValueError):
    pass


def check(ok, why):
    if not ok:
        raise Invalid(why)


def text(value, most, why):
    check(isinstance(value, str) and len(value) <= most, why)
    return value


def number(value, why):
    check(type(value) in (int, float) and -1e15 <= value <= 1e15, why)
    return value


def scalar(value, most, why):
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return text(value, most, why)
    return number(value, why)


def fields(value, allowed, why):
    check(isinstance(value, dict) and set(value) <= allowed, why)
    return value


def table(value, key_pattern, most_keys, most_text, why):
    check(isinstance(value, dict) and len(value) <= most_keys, why)
    for key, item in value.items():
        check(key_pattern.fullmatch(key), why)
        scalar(item, most_text, why)
    return value


def validate(data):
    """The report as kept, or Invalid. Every field is known; nothing else gets in."""
    allowed = {'version', 'kind', 'id', 'session', 'message', 'note', 'uptime', 'build', 'system',
               'game', 'settings', 'stack', 'log', 'replay_bytes'}
    check(isinstance(data, dict) and set(data) <= allowed, 'unknown fields')
    check(data.get('version') == 1, 'version')
    check(data.get('kind') in KINDS, 'kind')
    check(isinstance(data.get('id'), str) and HEX16.fullmatch(data['id']), 'id')
    if 'session' in data:
        check(isinstance(data['session'], str) and HEX16.fullmatch(data['session']), 'session')
    text(data.get('message', ''), 600, 'message')
    text(data.get('note', ''), 1000, 'note')
    if 'uptime' in data:
        check(type(data['uptime']) is int and 0 <= data['uptime'] < 10**9, 'uptime')
    if 'replay_bytes' in data:
        check(type(data['replay_bytes']) is int and 0 <= data['replay_bytes'] < 10**9, 'replay_bytes')
    build = fields(data.get('build', {}), {'version', 'commit', 'date', 'release', 'label'}, 'build')
    for key, item in build.items():
        if key == 'release':
            check(isinstance(item, bool), 'build')
        else:
            check(isinstance(item, str) and STAMP.fullmatch(item), 'build')
    system = fields(data.get('system', {}), {'os', 'platform', 'cores', 'ram_mb', 'gpu'}, 'system')
    for item in system.values():
        scalar(item, 300, 'system')
    table(data.get('game', {}), NAME, 32, 200, 'game')
    table(data.get('settings', {}), SETTING, 96, 64, 'settings')
    for name, most, longest in (('stack', 70, 300), ('log', 2000, 400)):
        lines = data.get(name, [])
        check(isinstance(lines, list) and len(lines) <= most, name)
        for line in lines:
            text(line, longest, name)
    return data


def decoded(value, limit, why):
    check(isinstance(value, str) and len(value) <= limit * 4 // 3 + 8, why)
    try:
        return base64.b64decode(value, validate=True)
    except ValueError as error:
        raise Invalid(why) from error


def picture(shot):
    """The picture's bytes, or Invalid: a JPEG only."""
    raw = decoded(shot, SHOT_LIMIT, 'shot')
    check(raw.startswith(b'\xff\xd8\xff'), 'shot')
    return raw


def fight(replay):
    """The fight's record, or Invalid: packed (PCFIGHZ1) or raw (PCFIGHT1)."""
    raw = decoded(replay, REPLAY_LIMIT, 'replay')
    check(raw[:8] in (b'PCFIGHZ1', b'PCFIGHT1'), 'replay')
    return raw


class Limits:
    """Per address an hour, everyone ten minutes; addresses only ever in memory."""

    def __init__(self):
        self.peers = {}
        self.everyone = collections.deque()

    def allow(self, address, now):
        for key in [key for key, times in self.peers.items() if times[-1] < now - 3600]:
            del self.peers[key]
        times = self.peers.get(address, collections.deque())
        while times and times[0] < now - 3600:
            times.popleft()
        while self.everyone and self.everyone[0] < now - 600:
            self.everyone.popleft()
        if len(times) >= PER_IP_HOUR or len(self.everyone) >= ALL_TEN_MINUTES or \
                (address not in self.peers and len(self.peers) >= 4096):
            return False
        times.append(now)
        self.peers[address] = times
        self.everyone.append(now)
        return True


def prune(folder):
    kept = sorted(folder.iterdir(), key=lambda path: path.stat().st_mtime)
    for path in kept[:max(0, len(kept) - FILES_KEPT)]:
        path.unlink(missing_ok=True)


def create_app(directory):
    directory = Path(directory)
    shots, replays = directory / 'shots', directory / 'replays'
    shots.mkdir(parents=True, exist_ok=True)
    replays.mkdir(parents=True, exist_ok=True)
    logger = logging.Logger('openpcrash-reports')
    handler = RotatingFileHandler(directory / 'reports.jsonl', maxBytes=8 * 1024 * 1024, backupCount=5)
    handler.setFormatter(logging.Formatter('%(message)s'))
    logger.addHandler(handler)
    limits = Limits()

    async def receive(request):
        if request.headers.get('X-OpenPCrash-Token') != TOKEN:
            return web.Response(status=403)
        if not limits.allow(request.headers.get('X-Real-IP', request.remote), time.monotonic()):
            return web.Response(status=429)
        try:
            body = await request.read()
        except web.HTTPRequestEntityTooLarge:
            return web.Response(status=413)
        try:
            data = json.loads(body)
            check(isinstance(data, dict), 'not an object')
            shot = data.pop('shot', None)
            replay = data.pop('replay', None)
            check(len(json.dumps(data, separators=(',', ':'))) <= JSON_LIMIT, 'too big')
            validate(data)
            check(shot is None or data['kind'] == 'manual', 'shot')
            jpeg = picture(shot) if shot is not None else None
            record = fight(replay) if replay is not None else None
        except (ValueError, TypeError, KeyError, RecursionError):
            return web.Response(status=400)
        kept = {'received': int(time.time()), **data}
        if jpeg:
            (shots / (data['id'] + '.jpg')).write_bytes(jpeg)
            kept['shot'] = data['id'] + '.jpg'
            prune(shots)
        if record:
            (replays / (data['id'] + '.pcfight')).write_bytes(record)
            kept['replay'] = data['id'] + '.pcfight'
            prune(replays)
        logger.info(json.dumps(kept, separators=(',', ':'), allow_nan=False))
        return web.Response(status=204)

    async def health(request):
        return web.Response(text='ok\n')

    async def close(app):
        handler.close()

    app = web.Application(client_max_size=BODY_LIMIT)
    app.router.add_post('/openpcrash-reports', receive)
    app.router.add_get('/openpcrash-reports/health', health)
    visits.add_routes(app, directory)
    app.on_cleanup.append(close)
    return app


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dir', default='/var/lib/openpcrash-reports')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8796)
    options = parser.parse_args()
    web.run_app(create_app(options.dir), host=options.host, port=options.port, access_log=None,
                print=None)
