#!/usr/bin/env python3
"""
gen_horizons_catalog.py -- build the bundled JPL Horizons mission catalog.

WHAT THIS DOES
--------------
Queries the Horizons text API for a configurable list of search keywords,
parses the fixed-width "* multiple matches *" tables, dedupes by target id,
and then *probes each target's real coverage window* with the vector ephemeris
endpoint.  The result is emitted as a generated C++ header:

    src/data/horizons_catalog_data.h

which holds `static const HorizonsCatalogEntry kHorizonsCatalog[]` plus a
separate `kHorizonsReferenceBodies[]` table (Sun, Moon, planets, L1/L2).

WHY THE WINDOW IS PROBED, NOT COMPUTED
--------------------------------------
Horizons coverage starts and ends *mid-day*.  Measured examples:

    -399110  starts 1969-JUL-16 16:40:02.7475 TDB
    -399101  starts 1969-MAY-23 05:37:54.1078 TDB
    -1024    ends   2026-APR-10 23:54:22.8576 TDB

A calendar-date-only window therefore loses rows on both ends.  The probe
reads the real timestamps out of the response rows / the
"No ephemeris for target ... prior to A.D. <t>" tails and stores them.

PROBE STRATEGY (at most two useful requests per target)
-------------------------------------------------------
1. START 1600-01-01, STOP 2500-01-01, STEP 100 d.
   - If rows come back, the first row timestamp is t0.
   - Otherwise the response either carries a "prior to A.D. <t0>" tail or an
     "after A.D. <t1>" tail (target entirely outside the window).
2. START <t0 + 1 s>, STOP 2500-01-01, STEP 100 d -> rows plus (usually) an
   "after A.D. <t1>" tail which is the exact coverage end.

Both responses are cached on disk under --cache-dir keyed by the request URL,
so re-running the generator costs zero network requests.

CLAMP RULE (crewed / lunar only)
--------------------------------
If a Crewed or Lunar entry's derived span exceeds 120 days it is trimmed to
120 days from t0.  Otherwise the derived span is stored verbatim.

USAGE
-----
  python scripts/gen_horizons_catalog.py
  python scripts/gen_horizons_catalog.py --no-probe --limit 40
  python scripts/gen_horizons_catalog.py --keywords "Apollo,Artemis" --sleep 0.5
  python scripts/gen_horizons_catalog.py --insecure     # corporate TLS proxy

--INSECURE USAGE NOTE
--------------------
`--insecure` disables TLS certificate verification. It exists ONLY because the
author's local corporate network terminates TLS with an interception proxy that
presents a self-signed certificate chain; without the flag the probe cannot
reach ssd.jpl.nasa.gov from that machine. It is a local-workaround escape hatch
and must NOT be used in CI or in any automated/shared environment - the default
(verifying) mode is the correct one everywhere else.

Stdlib only: urllib.request, json, argparse, time, re, ssl, os, sys,
datetime, tempfile.
"""

import argparse
import datetime
import json
import os
import re
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

API = "https://ssd.jpl.nasa.gov/api/horizons.api"

DEFAULT_KEYWORDS = [
    "spacecraft", "Apollo", "Artemis", "Soyuz", "Shenzhou", "Voyager",
    "Pioneer", "Mariner", "Viking", "Galileo", "Cassini", "Juno",
    "New Horizons", "Parker", "JWST", "Hubble", "LRO", "Chang'e",
    "Chandrayaan", "Hayabusa", "Rosetta", "BepiColombo", "Lucy", "Psyche",
    "Europa Clipper", "DART", "OSIRIS", "SOHO", "STEREO", "ACE", "Wind",
    "DSCOVR", "TESS", "Kepler", "Spitzer", "Chandra", "Swift", "Fermi",
    "MRO", "MAVEN", "Odyssey", "MESSENGER", "Venus Express", "Akatsuki",
    "Dawn", "Vesta", "Hera", "SLIM", "KPLO", "Danuri",
]

# The reference-body picker group. `command` is the Horizons *body number*;
# the client builds CENTER='500@<cmd>' from 500@399 pinning, so these are used
# as COMMAND only for reference-body geometry, never as a mission polyline.
REFERENCE_BODIES = [
    ("Sun", "10"),
    ("Moon", "301"),
    ("Earth", "399"),
    ("Mercury", "199"),
    ("Venus", "299"),
    ("Mars", "499"),
    ("Jupiter", "599"),
    ("Saturn", "699"),
    ("Uranus", "799"),
    ("Neptune", "899"),
    ("Sun-Earth L1", "31"),
    ("Sun-Earth L2", "32"),
    ("Earth-Moon L1", "34"),
    ("Earth-Moon L2", "35"),
]

# Explicit group overrides that the name heuristics would get wrong.
# Keyed by Horizons id.  Wins over everything else.
GROUP_BY_ID = {
    "-399080": "Lunar", "-399090": "Lunar", "-399100": "Lunar",
    "-399101": "Lunar", "-399110": "Lunar", "-399120": "Lunar",
    "-170": "L1/L2",      # JWST (Sun-Earth L2)
    "-85": "Lunar",       # LRO
}

# Acronyms that must not be mistaken for a truncated word.
KEEP_ACRONYMS = {"lm", "csm", "ss", "rb", "icps", "sr", "src", "cnsa",
                 "isro", "jaxa", "esa", "nasa", "sivb"}

# Name-substring overrides, checked before the generic heuristics.
GROUP_BY_NAME = [
    ("jwst", "L1/L2"),
    ("james webb", "L1/L2"),
    ("gaia", "L1/L2"),
    ("s-pekt", "L1/L2"),
    ("spektr", "L1/L2"),
]

# Strong, unambiguous name signals. Checked before the keyword group so that
# "Apollo 11 S-IVB" lands in Lunar while "Apollo 7 CSM" lands in Crewed.
LUNAR_STRONG = (
    "s-ivb", "s-iv", " lm", "lm (", "lm,", "lunar", "chang'e", "chandrayaan",
    "chandrayaan", "slim", "kplo", "danuri", "lro", "capstone", "blue ghost",
    "hakuto", "pragyan", "vikram", "grail", "lcross", "ladee", "clementine",
    "smart-1", "kaguya", "selene", "queqiao", "tiandu", "nova-c", "im-1",
    "im-2", "artemis 1 upper", "icps",
)
CREWED_STRONG = (
    "apollo", "artemis", "soyuz", "shenzhou", "crew dragon", "gemini",
    "space shuttle", "orion", "starliner", "dream chaser", "csm", "command module",
    "mercury capsule", "voskhod", "vostok", "taikonaut",
)
# Word-boundary matching is mandatory here: a plain substring test would match
# "ace" inside "space telescope" and put Hubble in L1/L2.
L1L2_RE = re.compile(
    r"(?<![a-z])(soho|ace|wind|dscovr|stereo|sdo|jwst|gaia|spektr|aditya|"
    r"solar orbiter|lagrange|dsco)(?![a-z])"
    r"|\bl[12]\b|\(l[12]\)|, l[12]",
    re.I,
)
INTERPLANETARY_HINTS = (
    "voyager", "pioneer", "mariner", "viking", "galileo", "cassini", "juno",
    "new horizons", "parker", "hubble", "tess", "kepler", "spitzer",
    "chandra", "swift", "fermi", "mro", "mars", "maven", "odyssey",
    "messenger", "venus", "akatsuki", "dawn", "vesta", "ceres", "hera",
    "lucy", "psyche", "europa clipper", "dart", "osiris", "rosetta",
    "bepicolombo", "hayabusa", "ulysses", "magellan", "deep space",
    "deep impact", "stardust", "genesis", "solar probe", "juice",
    "exomars", "phobos", "curiosity", "perseverance", "opportunity",
    "spirit", "ingenuity", "venera", "vega", "mangalyaan", "hope",
    "tianwen", "orbiter", "lander", "rover", "comet", "asteroid",
    "interplanetary", "heliocentric", "solar wind", "probe",
)

# Group implied by the search keyword that first surfaced an entry. This is the
# most reliable signal for mission families whose display names are opaque
# ("Planet-C" is Venus Climate Orbiter -> Akatsuki).
KEYWORD_GROUP = {
    "apollo": "Crewed", "artemis": "Crewed", "soyuz": "Crewed",
    "shenzhou": "Crewed",
    "voyager": "Interplanetary", "pioneer": "Interplanetary",
    "mariner": "Interplanetary", "viking": "Interplanetary",
    "galileo": "Interplanetary", "cassini": "Interplanetary",
    "juno": "Interplanetary", "new horizons": "Interplanetary",
    "parker": "Interplanetary", "hubble": "Interplanetary",
    "hayabusa": "Interplanetary", "rosetta": "Interplanetary",
    "bepicolombo": "Interplanetary", "lucy": "Interplanetary",
    "psyche": "Interplanetary", "europa clipper": "Interplanetary",
    "dart": "Interplanetary", "osiris": "Interplanetary",
    "tess": "Interplanetary", "kepler": "Interplanetary",
    "spitzer": "Interplanetary", "chandra": "Interplanetary",
    "swift": "Interplanetary", "fermi": "Interplanetary",
    "mro": "Interplanetary", "maven": "Interplanetary",
    "odyssey": "Interplanetary", "messenger": "Interplanetary",
    "venus express": "Interplanetary", "akatsuki": "Interplanetary",
    "dawn": "Interplanetary", "vesta": "Interplanetary",
    "hera": "Interplanetary",
    "jwst": "L1/L2", "soho": "L1/L2", "stereo": "L1/L2", "ace": "L1/L2",
    "wind": "L1/L2", "dscovr": "L1/L2",
    "lro": "Lunar", "chang'e": "Lunar", "chandrayaan": "Lunar",
    "slim": "Lunar", "kplo": "Lunar", "danuri": "Lunar",
}

# Curated priority: entries whose name matches an earlier pattern are kept
# first when --limit truncates the set. Everything not listed ranks last.
CURATED_ORDER = [
    # crewed / lunar
    "apollo 11", "apollo 10", "apollo 8", "apollo 9", "apollo 12",
    "artemis i", "artemis ii", "artemis 1", "artemis 2",
    "lro", "lunar reconnaissance", "chandrayaan", "chang'e", "slim",
    "danuri", "kplo", "capstone", "clementine", "grail", "lcross", "ladee",
    # interplanetary
    "voyager 1", "voyager 2", "pioneer 10", "pioneer 11", "mariner",
    "viking", "galileo", "cassini", "juno", "new horizons", "parker",
    "hubble", "jwst", "james webb", "roman",
    "mro", "mars reconnaissance", "maven", "mars odyssey", "messenger",
    "venus express", "akatsuki", "planet-c", "dawn", "rosetta",
    "bepicolombo", "hayabusa", "lucy", "psyche", "europa clipper", "dart",
    "juice", "ulysses", "magellan", "stardust", "deep impact", "deep space",
    # L1/L2 and heliophysics
    "soho", "ace", "wind", "dscovr", "stereo", "solar orbiter", "gaia",
    "spektr", "aditya", "tess", "kepler", "spitzer", "chandra", "swift",
    "fermi", "sdo",
]

# Round-robin order used when --limit truncates the set.
INTERESTING_GROUPS = ["Crewed", "Lunar", "Interplanetary", "L1/L2", "Other"]

STEP_TABLE = [
    (3.0, "10 m"),
    (30.0, "30 m"),
    (180.0, "2 h"),
    (3 * 365.25, "12 h"),
]


# --------------------------------------------------------------------------
# HTTP with a disk cache
# --------------------------------------------------------------------------

def _cache_name(url):
    """Deterministic, filesystem-safe cache key for a URL."""
    h = 5381
    for ch in url:
        h = ((h * 33) + ord(ch)) & 0xFFFFFFFF
    slug = re.sub(r"[^A-Za-z0-9]+", "_", url)[-90:]
    return "%08x_%s.txt" % (h, slug)


def make_fetcher(cache_dir, sleep_s, insecure, timeout=60, verbose=False):
    ctx = ssl._create_unverified_context() if insecure else None
    state = {"requests": 0, "cache_hits": 0}

    def fetch(url):
        path = None
        if cache_dir:
            os.makedirs(cache_dir, exist_ok=True)
            path = os.path.join(cache_dir, _cache_name(url))
            if os.path.isfile(path) and os.path.getsize(path) > 0:
                state["cache_hits"] += 1
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    return fh.read()

        if sleep_s:
            time.sleep(sleep_s)
        state["requests"] += 1
        if verbose:
            sys.stderr.write("GET %s\n" % url)

        try:
            req = urllib.request.Request(url, headers={"User-Agent": "TLEscope-catalog-gen/1.0"})
            with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp:
                raw = resp.read()
        except urllib.error.HTTPError as exc:
            raw = exc.read() or b""
            if verbose:
                sys.stderr.write("  HTTP %s\n" % exc.code)
        except Exception as exc:                       # noqa: BLE001 - report, keep going
            if verbose:
                sys.stderr.write("  request failed: %s\n" % exc)
            raw = b""

        text = raw.decode("utf-8", "replace")
        if path and text:
            try:
                with open(path, "w", encoding="utf-8", errors="replace") as fh:
                    fh.write(text)
            except OSError:
                pass
        return text

    fetch.stats = state
    return fetch


def q(value):
    """Percent-encode a Horizons parameter value, keeping the quotes literal."""
    return urllib.parse.quote("'%s'" % value, safe="':@-._")


def search_url(keyword):
    return "%s?format=text&COMMAND=%s" % (API, q(keyword))


def vectors_url(command, start, stop, step):
    return (
        "%s?format=text&COMMAND=%s&EPHEM_TYPE=VECTORS&CENTER=%s"
        "&START_TIME=%s&STOP_TIME=%s&STEP_SIZE=%s&VEC_TABLE=%s&CSV_FORMAT=%s"
        % (API, q(command), q("500@399"), q(start), q(stop), q(step), q("1"), q("YES"))
    )


# --------------------------------------------------------------------------
# Match-table parsing
# --------------------------------------------------------------------------

def _column_starts(dashes_line):
    """Character offsets of the `-----` runs in the table header separator."""
    return [m.start() for m in re.finditer(r"-{5,}", dashes_line)]


def parse_matches(text):
    """Parse a Horizons multiple-matches table into [(id, name, designation, nickname)]."""
    lines = text.splitlines()
    out = []
    cols = None

    for i, line in enumerate(lines):
        stripped = line.strip()
        if cols is None and set(stripped) <= set("- ") and len(stripped) > 20 and "ID#" in lines[i - 1]:
            cols = _column_starts(line)
            if len(cols) >= 3:
                continue
            cols = None
            continue

        if cols is None:
            continue
        if not stripped:
            if out:
                break
            continue
        if stripped.startswith("Number of matches") or stripped.startswith("*"):
            break

        def field(idx, end_idx):
            if idx >= len(cols):
                return ""
            a = cols[idx]
            b = cols[end_idx] if (end_idx is not None and end_idx < len(cols)) else None
            return line[a:b].strip() if a < len(line) else ""

        raw_id = field(0, 1)
        name = field(1, 2)
        desig = field(2, 3) if len(cols) > 2 else ""
        nick = line[cols[3]:].strip() if len(cols) > 3 else ""

        # Starred rows are the "non-major body" marker in small-body tables.
        if "*" in raw_id:
            continue
        if not re.fullmatch(r"-?\d+", raw_id):
            continue
        if not name:
            continue

        # A designation is COSPAR-ish ("1969-059C"); anything else in that
        # column is prose, keep it out of the label.
        if desig and not re.fullmatch(r"[12]\d{3}-[0-9A-Za-z]+[A-Za-z]?", desig):
            nick = (desig + " " + nick).strip()
            desig = ""

        out.append((raw_id, name, desig, nick))

    return out


ROW_RE = re.compile(
    r"^\s*[\d.]+,\s*A\.D\.\s+(\d{4})-([A-Za-z]{3})-(\d{2})\s+(\d{2}):(\d{2}):([\d.]+)"
)
# The seconds/fraction group is REQUIRED, not cosmetic: coverage starts and ends
# mid-day (1969-JUL-16 16:40:02.7475).  Dropping it makes the probe believe t0 is
# exactly 16:40:00, which is still before coverage, so Horizons answers
# "prior to A.D." and returns an empty table on every retry.
TAIL_PRIOR_RE = re.compile(
    r"prior to A\.D\.\s+(\d{4})-([A-Za-z]{3})-(\d{2})\s+(\d{2}):(\d{2})(?::([\d.]+))?")
TAIL_AFTER_RE = re.compile(
    r"after\s+A\.D\.\s+(\d{4})-([A-Za-z]{3})-(\d{2})\s+(\d{2}):(\d{2})(?::([\d.]+))?")

MONTHS = {m: i + 1 for i, m in enumerate(
    ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"])}


def _dt(match):
    """Full-precision datetime from a match; keeps the seconds/fraction when the
    regex captured them, so a coverage start of 16:40:02.7475 is distinguishable
    from an exact 16:40:00."""
    year, mon, day = match.group(1), match.group(2).upper(), match.group(3)
    hh, mm = match.group(4), match.group(5)
    sec = 0.0
    if match.lastindex and match.lastindex >= 6 and match.group(6):
        sec = float(match.group(6))
    base = datetime.datetime(int(year), MONTHS.get(mon, 1), int(day), int(hh), int(mm))
    return base + datetime.timedelta(seconds=sec)


def first_last_rows(text):
    first = last = None
    for line in text.splitlines():
        m = ROW_RE.match(line)
        if not m:
            continue
        ts = _dt(m)
        if first is None:
            first = ts
        last = ts
    return first, last


def coverage_tail(text, kind):
    regex = TAIL_PRIOR_RE if kind == "prior" else TAIL_AFTER_RE
    m = regex.search(text)
    return _dt(m) if m else None


# --------------------------------------------------------------------------
# Probes
# --------------------------------------------------------------------------

def _safe_start(ts):
    """A START_TIME guaranteed to be inside coverage.

    `ts` is the parsed coverage start; when the tail carried a sub-second
    fraction it is preserved. Horizons rejects a window that begins even a
    microsecond before coverage ("prior to A.D." + empty table), so round up
    to the next whole minute.
    """
    exact = ts.replace(second=0, microsecond=0)
    if ts != exact:
        exact += datetime.timedelta(minutes=1)
    return exact.strftime("%Y-%m-%d %H:%M:%S")


# Horizons only emits a table when the ENTIRE requested window lies inside the
# target's coverage; otherwise it returns an empty table plus a
# "prior to A.D. <t0>" / "after A.D. <t>" tail naming the real bound.  It also
# has a hard data-availability horizon (measured 2026-11-08: Voyager 1 answers
# "after A.D. 2100-JAN-01"), which is why the literal 2500-01-01 STOP from the
# spec produces an empty table for every target.
#
# So the probe needs two bounded requests:
#   1. locate t0 (rows start, or a "prior to" tail),
#   2. start just after t0 and stop at the availability horizon; the bound is
#      then either an "after A.D." tail (exact) or the last emitted row.

PROBE_STEP = "100 d"                    # coarse: only endpoints matter
AVAILABILITY_STOPS = ("2100-01-01", "2049-01-01", "2035-01-01")


def _probe_end(fetch, command, t0, verbose):
    """Coverage end for a target whose coverage starts at t0."""
    for stop in AVAILABILITY_STOPS:
        text = fetch(vectors_url(command, _safe_start(t0), stop, PROBE_STEP))
        rows_first, rows_last = first_last_rows(text)
        after = coverage_tail(text, "after")
        prior = coverage_tail(text, "prior")
        if prior is not None:
            continue                     # t0 was rejected; try a later stop
        if after is not None:
            return after                 # exact coverage end
        if rows_last is not None:
            return rows_last             # coverage runs to the availability limit
        # Empty table with no usable tail: try the next (shorter) stop.
    if verbose:
        sys.stderr.write("  no end bound for %s\n" % command)
    return None


def probe_window(fetch, command, verbose=False):
    """Return (t0, t1) datetimes for a target's real coverage, or (None, None)."""
    text = fetch(vectors_url(command, "1600-01-01", AVAILABILITY_STOPS[0], PROBE_STEP))
    first, _last = first_last_rows(text)
    t0 = first if first is not None else coverage_tail(text, "prior")
    if t0 is None:
        if verbose:
            sys.stderr.write("  no start bound for %s\n" % command)
        return None, None
    t1 = _probe_end(fetch, command, t0, verbose)
    if t1 is None or t1 <= t0:
        return t0, None
    return t0, t1


def choose_step(span_days):
    for limit, label in STEP_TABLE:
        if span_days <= limit:
            return label
    return "1 d"


def fmt(ts):
    """Store "YYYY-MMM-DD HH:MM" (what Horizons accepts and the client parses)."""
    return ts.strftime("%Y-") + ts.strftime("%b").upper() + ts.strftime("-%d %H:%M")


def fmt_start(ts):
    """Format a coverage START that is guaranteed to be INSIDE coverage.

    The tail text carries sub-second precision that we truncate away:
    Apollo 11 S-IVB really starts 1969-JUL-16 16:40:02.7475.  Storing
    "16:40" and asking START_TIME='16:40' is still *before* the bound, so
    Horizons answers "prior to A.D." and emits an empty table.  Rounding the
    start UP to the next whole minute always lands inside coverage.
    """
    exact = ts.replace(second=0, microsecond=0)
    if ts != exact:
        exact += datetime.timedelta(minutes=1)
    return fmt(exact)


# --------------------------------------------------------------------------
# Grouping
# --------------------------------------------------------------------------

def assign_group(command, name, keyword="", verbose=False):
    """Best-effort group for one entry.

    Order matters:
      1. explicit id override,
      2. explicit name override (JWST etc.),
      3. unambiguous strong name signals (Lunar before Crewed, so
         "Apollo 11 S-IVB" is Lunar while "Apollo 7 CSM" is Crewed),
      4. the search keyword that surfaced the entry (opaque names such as
         "Planet-C" -> Akatsuki),
      5. broad interplanetary / L1/L2 substrings,
      6. Other.
    """
    if command in GROUP_BY_ID:
        return GROUP_BY_ID[command]
    low = name.lower()
    for needle, group in GROUP_BY_NAME:
        if needle in low:
            return group
    if any(k in low for k in LUNAR_STRONG):
        return "Lunar"
    if any(k in low for k in CREWED_STRONG):
        return "Crewed"
    kw = (keyword or "").lower()
    if kw in KEYWORD_GROUP:
        # The keyword only implies a group when the name agrees. THEMIS-B/C are
        # the ARTEMIS magnetospheric probes (lunar orbit), not the Artemis
        # programme, and they surface under the "Artemis" search.
        if kw == "artemis" and not any(k in low for k in
                                       ("artemis", "orion", "sls", "icps")):
            return "Lunar" if "themis" in low else "Other"
        return KEYWORD_GROUP[kw]
    if L1L2_RE.search(low):
        return "L1/L2"
    if any(k in low for k in INTERPLANETARY_HINTS):
        return "Interplanetary"
    return "Other"


def clean_name(raw):
    """Tidy a Horizons display name into a combo label.

    The match table is fixed-width, so long names are truncated mid-word
    ("James Webb Space Telescope (spacecr)").  Strategy:
      - drop a trailing "(spacecraft)" marker,
      - drop a trailing truncated parenthetical ("(spacecr", "(LANDER spa"),
      - drop a trailing truncated word fragment,
      - collapse whitespace and underscores.
    """
    name = raw.strip()
    name = re.sub(r"\s*\((?:spacecraft|spacec|space|spacecr)\)?\s*$", "", name, flags=re.I)
    # "(LANDER spacecraft)" -> "(LANDER)" -> "LANDER" is content, keep the word.
    name = re.sub(r"\s+spacecraft\)\s*$", ")", name, flags=re.I)
    # Any unterminated trailing "(" ... is truncation, not content.
    if name.count("(") > name.count(")"):
        name = name[: name.rindex("(")].rstrip()
    name = name.replace("_", " ")
    name = re.sub(r"\s+", " ", name).strip(" ,-")
    # A trailing fragment with no vowel and <=3 chars is usually a cut-off word,
    # but real acronyms (LM, CSM, SS, RB) must survive.
    m = re.search(r"\s([A-Za-z]{1,3})$", name)
    if m and not re.search(r"[aeiouAEIOU]", m.group(1)) \
            and m.group(1).lower() not in KEEP_ACRONYMS:
        name = name[: m.start()].rstrip()
    return name


def curated_rank(name):
    low = name.lower()
    for i, needle in enumerate(CURATED_ORDER):
        if needle in low:
            return i
    return len(CURATED_ORDER)


# --------------------------------------------------------------------------
# Emission
# --------------------------------------------------------------------------

_ASCII = {"'": "'", "\u2019": "'", "\u2018": "'", "\u201c": '"', "\u201d": '"',
          "\u2013": "-", "\u2014": "-", "\u00e9": "e", "\u00e8": "e", "\u00fc": "u",
          "\u00f6": "o", "\u00e4": "a", "\u00df": "ss", "\u00d8": "O", "\u00e5": "a"}


def c_escape(text):
    out = []
    for ch in text:
        if ch in _ASCII:
            ch = _ASCII[ch]
        if ord(ch) < 32:
            continue
        if ord(ch) > 126:
            continue                     # drop anything still non-ASCII
        if ch in '"\\':
            out.append("\\" + ch)
        else:
            out.append(ch)
    return "".join(out)


def emit_header(path, entries, refs, generated_on, sources):
    rows = []
    for e in entries:
        rows.append(
            '    { "%s", "%s", "%s", "%s", "%s", "%s" },'
            % (c_escape(e["name"]), c_escape(e["command"]), c_escape(e["start"]),
               c_escape(e["stop"]), c_escape(e["step"]), c_escape(e["group"]))
        )
    ref_rows = []
    for e in refs:
        ref_rows.append(
            '    { "%s", "%s", "%s", "%s", "%s", "%s" },'
            % (c_escape(e["name"]), c_escape(e["command"]), c_escape(e["start"]),
               c_escape(e["stop"]), c_escape(e["step"]), c_escape(e["group"]))
        )

    lines = []
    lines.append("/* GENERATED - do not edit; run scripts/gen_horizons_catalog.py */")
    lines.append("/*")
    lines.append(" * Generated: %s" % generated_on)
    lines.append(" * Sources:   NASA/JPL Horizons API (%s)" % API)
    lines.append(" * Entries:   %d missions + %d reference bodies" % (len(entries), len(refs)))
    lines.append(" *")
    lines.append(" * Windows are probe-derived real coverage bounds (YYYY-MMM-DD HH:MM).")
    lines.append(" * Header is included only from horizons_api.cpp, hence `static`.")
    lines.append(" */")
    lines.append("#ifndef HORIZONS_CATALOG_DATA_H")
    lines.append("#define HORIZONS_CATALOG_DATA_H")
    lines.append("")
    lines.append("/* Requires HorizonsCatalogEntry to be declared first. */")
    lines.append("")
    lines.append("static const HorizonsCatalogEntry kHorizonsCatalog[] = {")
    lines.extend(rows)
    lines.append("};")
    lines.append("static const int kHorizonsCatalogCount = %d;" % len(entries))
    lines.append("")
    lines.append("static const HorizonsCatalogEntry kHorizonsReferenceBodies[] = {")
    lines.extend(ref_rows)
    lines.append("};")
    lines.append("static const int kHorizonsReferenceBodyCount = %d;" % len(refs))
    lines.append("")
    lines.append("#endif /* HORIZONS_CATALOG_DATA_H */")
    lines.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate the bundled Horizons mission catalog.")
    ap.add_argument("--out", default=os.path.join("src", "data", "horizons_catalog_data.h"))
    ap.add_argument("--limit", type=int, default=0, help="cap mission entries (0 = no cap)")
    ap.add_argument("--no-probe", action="store_true", help="skip coverage probes; leaves windows empty")
    ap.add_argument("--keywords", default="", help="comma-separated search keywords")
    ap.add_argument("--cache-dir", default=os.path.join("build", "horizons_catalog_cache"))
    ap.add_argument("--sleep", type=float, default=0.3, help="seconds between HTTP requests")
    ap.add_argument("--insecure", action="store_true", help="skip TLS verification (corporate proxy)")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    keywords = [k.strip() for k in args.keywords.split(",") if k.strip()] or DEFAULT_KEYWORDS
    fetch = make_fetcher(args.cache_dir, args.sleep, args.insecure, args.timeout, args.verbose)

    started = time.time()
    seen = {}
    skipped = []
    skipped_ids = []

    for kw in keywords:
        text = fetch(search_url(kw))
        matches = parse_matches(text)
        if args.verbose:
            sys.stderr.write("  keyword %-20s -> %d matches\n" % (kw, len(matches)))
        for raw_id, name, desig, nick in matches:
            if raw_id in seen:
                # A later, more specific keyword can group an entry that the
                # generic "spacecraft" sweep left in Other (e.g. "Planet-C"
                # is Akatsuki and belongs in Interplanetary).
                if seen[raw_id]["group"] == "Other":
                    better = assign_group(raw_id, seen[raw_id]["name"], kw)
                    if better != "Other":
                        seen[raw_id]["group"] = better
                continue
            # Small-body target ids are positive integers ("2000016" = Psyche);
            # they are not catalog missions and the VECTORS probe on them is
            # slow and unreliable. Keep only spacecraft/stage ids (-N).
            if not raw_id.startswith("-"):
                skipped_ids.append((raw_id, clean_name(name)))
                continue
            base = clean_name(name) or name.strip()
            # Impact debris ("CONTOUR-A (SPACECRAFT FRAGMENT)") are not useful
            # trackable missions and only crowd out real entries.
            if "FRAGMENT" in name.upper() or "DEBRIS" in name.upper():
                skipped_ids.append((raw_id, base))
                continue
            seen[raw_id] = {
                "command": raw_id,
                "name": base,
                "designation": desig,
                "nickname": nick,
                "group": assign_group(raw_id, base, kw, args.verbose),
                "start": "",
                "stop": "",
                "step": "1 d",
            }

    found = len(seen)
    if args.limit > 0 and len(seen) > args.limit:
        # Round-robin across groups in INTERESTING_GROUPS order so a limited run
        # is a realistic cross-section rather than 60 near-duplicate Moon shots.
        by_group = {}
        discovery = {k: i for i, k in enumerate(seen.keys())}
        for cid, entry in seen.items():
            by_group.setdefault(entry["group"], []).append(cid)
        for g in by_group:
            by_group[g].sort(key=lambda k: (curated_rank(seen[k]["name"]), discovery[k]))

        keep = []
        order_groups = [g for g in INTERESTING_GROUPS if g in by_group]
        order_groups += [g for g in by_group if g not in order_groups]
        while len(keep) < args.limit:
            progressed = False
            for g in order_groups:
                if by_group[g] and len(keep) < args.limit:
                    keep.append(by_group[g].pop(0))
                    progressed = True
            if not progressed:
                break
        seen = {k: seen[k] for k in keep}

    entries = []
    probed = 0
    for cid, entry in seen.items():
        if args.no_probe:
            entries.append(entry)
            continue
        t0, t1 = probe_window(fetch, cid, args.verbose)
        if t0 is None or t1 is None:
            skipped.append((cid, entry["name"]))
            unlikely = True
            entry["start"] = entry["stop"] = ""
            entries.append(entry)
            continue

        span = (t1 - t0).total_seconds() / 86400.0
        # The stored window is always the probe-derived coverage span.  Short
        # crewed flights are naturally 8-30 days, and long-lived orbiters such
        # as LRO (2009 -> present) must keep their full span or the mission
        # would be unusable; the client's row cap already coarsens the step,
        # so no artificial trimming is applied here.
        entry["start"] = fmt_start(t0)
        entry["stop"] = fmt(t1)
        entry["step"] = choose_step(span)
        probed += 1
        entries.append(entry)
        if args.verbose:
            sys.stderr.write("  %-8s %-10s %-38s %s .. %s (%s)\n"
                             % (cid, entry["group"], entry["name"][:38],
                                entry["start"], entry["stop"], entry["step"]))

    refs = []
    for name, body_id in REFERENCE_BODIES:
        e = {"name": name, "command": body_id, "group": "Reference",
             "start": "1600-01-01 00:00", "stop": "2500-01-01 00:00", "step": "1 d"}
        if not args.no_probe:
            t0, t1 = probe_window(fetch, body_id, args.verbose)
            if t0 is not None and t1 is not None and t1 > t0:
                e["start"] = fmt(t0)
                e["stop"] = fmt(t1)
                e["step"] = choose_step((t1 - t0).total_seconds() / 86400.0)
        refs.append(e)

    out_dir = os.path.dirname(args.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    generated_on = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    emit_header(args.out, entries, refs, generated_on, keywords)

    elapsed = time.time() - started
    groups = {}
    for e in entries:
        groups[e["group"]] = groups.get(e["group"], 0) + 1

    print("Horizons catalog generated: %s" % args.out)
    print("  keywords searched : %d" % len(keywords))
    print("  entries found     : %d" % found)
    print("  entries written   : %d" % len(entries))
    print("  probed windows    : %d" % probed)
    print("  reference bodies  : %d" % len(refs))
    print("  skipped (no probe): %d" % len(skipped))
    for cid, name in skipped:
        print("    - %s %s" % (cid, name))
    if skipped_ids:
        print("  skipped (small-body ids): %d" % len(skipped_ids))
        for cid, name in skipped_ids[:10]:
            print("    - %s %s" % (cid, name))
    print("  groups            : %s"
          % ", ".join("%s=%d" % kv for kv in sorted(groups.items())))
    print("  http requests     : %d (cache hits %d)"
          % (fetch.stats["requests"], fetch.stats["cache_hits"]))
    print("  runtime           : %.1f s" % elapsed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
