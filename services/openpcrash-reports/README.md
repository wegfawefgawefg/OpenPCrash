# openpcrash-reports

Where OpenPCrash's bug reports go (docs/release.md, "Bug reports"), and the site's visit count (below). A small aiohttp service behind nginx: it takes a report
as JSON, checks every field against a fixed list and keeps it. There is no way to read reports back over
HTTP; the owner copies them off the box with `tools/reports.py pull`.

- `POST /openpcrash-reports` with the `X-OpenPCrash-Token` header (a static token in the game's source; it only
  keeps junk out). 204 kept, 400 not a report, 403 no token, 413 too big, 429 too many.
- `GET /openpcrash-reports/health` answers `ok`.
- Kinds: `crash`, `hang`, `renderer`, `startup`, `manual`. Anything else, or any field not on the list, is a 400.
- Limits: 256 KiB of JSON without the picture and the replay, a JPEG picture of 400 KiB (the player's own reports
  only), a packed fight record of 1.5 MiB, 3 MiB a request; 20 reports an hour from one address and 200 every ten
  minutes from everyone. The addresses are held in memory for those limits only, never written anywhere, and
  nginx keeps no access log for these locations.
- Kept in `/var/lib/openpcrash-reports/`: `reports.jsonl` (a JSON line each, rotated at 8 MiB, five kept),
  `shots/<id>.jpg` and `replays/<id>.pcfight` (the newest thousand of each).

## The site's visit count

`visits.py`: the beacon in `site/index.html` posts one small JSON body (as `text/plain`, so no CORS preflight)
for each page view and each click on a download link or the source link (docs/release.md, "Site visits").

- `POST /openpcrash-reports/visit`. Only `Origin: https://wegfawefgawefg.github.io` is taken (403 otherwise); the
  answer carries `Access-Control-Allow-Origin` for it. 204 kept (or dropped as a bot), 400 not a hit, 413 over
  2 KiB, 429 too many: 60 an hour from one address, 6000 every ten minutes from everyone.
- Kinds: `view`, `download-windows`, `download-mac`, `download-linux`, `source`. The page's fields (`path`,
  `referrer` host, `source` from `?ref=` or `?utm_source=`, `screen` bucket, `lang`) are kept only when they
  match their pattern; one that doesn't is left out, the hit is still kept.
- Bots are dropped: a User-Agent with bot, crawl, spider, headless, curl, python and the like, or none.
- Per hit, one JSON line in `visits-YYYY-MM.jsonl` (a month stops growing at 256 MiB): `time` (Unix, UTC),
  `kind`, `path`, `referrer`, `source`, `screen`, `lang`, `country` (ISO code) and `city` from the address,
  `browser` and `os` (a family name each from the User-Agent; the User-Agent itself isn't kept), and `visitor`.
- `visitor` is the first 16 hex digits of SHA-256 over a salt, the address and the User-Agent. The salt is random,
  held in memory only and replaced when the UTC day changes, so a day's hits from one browser share a hash and
  nothing links days or leads back to an address. A restart starts a new salt: that day's returning browsers
  count twice.
- The address is used for the place, the hash and the rate limit, in memory, and never written. nginx keeps no
  access or error log for this location.

The place comes from DB-IP's free "IP to City Lite" database (CC BY 4.0; the site's footer credits DB-IP, which
the licence requires), `/var/lib/openpcrash-reports/geoip/dbip-city-lite.mmdb`, read with `python3-maxminddb`.
`geoip.py` fetches it from download.db-ip.com (no account), checks it opens and places a known address, and
swaps it in; the server reopens it within a minute. `openpcrash-geoip.timer` runs it on the 4th of each month
(DB-IP publishes at the start of the month). Without the database or the module, hits are kept without a place.
Fetch it by hand with `systemctl start openpcrash-geoip.service`.

Install or update, as root on the box, with the nginx site that serves its HTTPS:

    scp -r services/openpcrash-reports root@HOST:/root/
    ssh root@HOST python3 /root/openpcrash-reports/install.py --site /etc/nginx/sites-enabled/SITE

The installer copies `server.py`, `visits.py` and `geoip.py` to `/opt/openpcrash-reports/`, installs
`openpcrash-reports.service` (127.0.0.1:8796, a dynamic user, its own state folder) and `openpcrash-geoip.timer`,
and adds whichever of the three locations the site lacks before its catch-all, after backing the site up
(`/var/backups/nginx/SITE.pre-openpcrash-reports-<time>`); a failed `nginx -t` puts the site back. It reloads
nginx, restarts only its own service, and fetches the GeoIP database when there is none. The box needs
`python3-aiohttp` and `python3-maxminddb`. On the dev server the site is `/etc/nginx/sites-enabled/gauche-roomd`,
a plain file.

Check it:

    curl -s https://HOST/openpcrash-reports/health
    ssh root@HOST systemctl status openpcrash-reports
    ssh root@HOST systemctl list-timers openpcrash-geoip.timer
