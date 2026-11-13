#!/usr/bin/env python3
"""Install the OpenPCrash report service: run as root on the server, beside server.py and its units.

Copies server.py, visits.py and geoip.py to /opt/openpcrash-reports, installs and starts
openpcrash-reports.service (127.0.0.1:8796, its own dynamic user, data in
/var/lib/openpcrash-reports) and openpcrash-geoip.timer (DB-IP's city database, monthly; fetched
now when there is none), and adds any of its nginx locations the site given lacks
(`/openpcrash-reports`, its health check, `/openpcrash-reports/visit`) to that site (the file
nginx loads, in sites-enabled/, serving HTTPS on the box), leaving every other location as it is:
the site is backed up first (to /var/backups/nginx/), checked with `nginx -t`, put back if that
fails, then nginx reloaded (never restarted). Nothing else is restarted. Run again to update.

    install.py --site /etc/nginx/sites-enabled/NAME
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import time

LOCATIONS = {
    'location = /openpcrash-reports {': '''    # OpenPCrash bug reports: no access log, so no player address is kept
    location = /openpcrash-reports {
        proxy_pass http://127.0.0.1:8796;
        proxy_set_header X-Real-IP $remote_addr;
        client_max_body_size 3m;
        proxy_read_timeout 20s;
        access_log off;
    }
''',
    'location = /openpcrash-reports/health {': '''    location = /openpcrash-reports/health {
        proxy_pass http://127.0.0.1:8796;
        proxy_read_timeout 5s;
        access_log off;
    }
''',
    'location = /openpcrash-reports/visit {': '''    # the OpenPCrash site's visit count: no access or error log, so no visitor address is kept
    location = /openpcrash-reports/visit {
        proxy_pass http://127.0.0.1:8796;
        proxy_set_header X-Real-IP $remote_addr;
        client_max_body_size 4k;
        proxy_read_timeout 5s;
        access_log off;
        error_log /dev/null;
    }
''',
}

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument('--site', required=True, help="the nginx site that serves the box's HTTPS")
options = parser.parse_args()

base = Path(__file__).resolve().parent
Path('/opt/openpcrash-reports').mkdir(mode=0o755, exist_ok=True)
for name in ('server.py', 'visits.py', 'geoip.py'):
    shutil.copy2(base / name, '/opt/openpcrash-reports/' + name)
for name in ('openpcrash-reports.service', 'openpcrash-geoip.service', 'openpcrash-geoip.timer'):
    shutil.copy2(base / name, '/etc/systemd/system/' + name)
try:
    import maxminddb  # noqa: F401
except ImportError:
    print('python3-maxminddb is missing: visits are kept without a country or city')

site = Path(options.site).resolve()  # the file nginx loads, through a link
original = site.read_text()
missing = [head for head in LOCATIONS if head not in original]
if missing:
    # before the server's catch-all in its HTTPS block (the last `location / { return 404; }`)
    marker = '    location / { return 404; }\n}'
    if not original.rstrip().endswith(marker.rstrip()) or original.count('listen 443') != 1:
        raise SystemExit('The nginx site is not the one expected; nothing changed')
    at = original.rstrip().rfind(marker.rstrip())
    # the backup out of nginx's folders (everything in sites-enabled/ is loaded)
    backups = Path('/var/backups/nginx')
    backups.mkdir(parents=True, exist_ok=True)
    backup = backups / (site.name + '.pre-openpcrash-reports-' + time.strftime('%Y%m%d%H%M%S'))
    backup.write_text(original)
    site.write_text(original[:at] + ''.join(LOCATIONS[head] for head in missing) + original[at:])
    if subprocess.run(['nginx', '-t']).returncode:
        site.write_text(original)
        raise SystemExit('nginx -t failed; the site is put back as it was')
    print(f"nginx: added {', '.join(head[:-2] for head in missing)}; backup at {backup}")

subprocess.run(['systemctl', 'daemon-reload'], check=True)
subprocess.run(['systemctl', 'enable', 'openpcrash-reports'], check=True)
subprocess.run(['systemctl', 'restart', 'openpcrash-reports'], check=True)
subprocess.run(['systemctl', 'reload', 'nginx'], check=True)
# after the service has made its state folder; the server opens the database within a minute
subprocess.run(['systemctl', 'enable', '--now', 'openpcrash-geoip.timer'], check=True)
if not Path('/var/lib/openpcrash-reports/geoip/dbip-city-lite.mmdb').exists():
    subprocess.run(['systemctl', 'start', 'openpcrash-geoip.service'], check=True)
print('openpcrash-reports running')
