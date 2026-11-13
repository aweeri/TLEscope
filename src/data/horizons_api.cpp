// JPL Horizons VECTORS client: URL build, async fetch, JSON unwrap, parse.
// The worker thread owns all network and parse work; the UI thread only starts a
// fetch, polls the atomic state and takes the result. One result slot is guarded
// by a std::mutex, the state is lock-free. Cancellation is cooperative.

#include "data/horizons_api.h"
#include "data/ephem_parse.h"
#include "data/provider.h"
#include "data/cache.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

// -- Endpoint ----------------------------------------------------------------

// 2026-11-08 measurement (recorded from a live probe, keep this note):
//   * https://ssd-api.jpl.nasa.gov/api/horizons.api  -> HTTP 404, 146-byte
//     nginx HTML body. That host only serves SBDB/Sentry/Scout; there is no
//     `horizons.api` path there. This was the original client bug.
//   * https://ssd.jpl.nasa.gov/api/horizons.api      -> HTTP 200.
//     Note the shape: `api` is a PATH segment, not a subdomain.
//   * The parameter encoding was verified NOT to be the cause: deliberately
//     double-encoded quotes still return HTTP 200 with an empty small-body
//     table, never 404. So the 404 was purely the wrong host.
static const char *const kHorizonsBaseUrl = "https://ssd.jpl.nasa.gov/api/horizons.api";

// The API rejects requests with more rows than this (and the response would be
// tens of megabytes). Measured: LRO target `-85` over 2026-04-01..26 at `10 m`
// -> HTTP 200, 3601 rows, ~710 KB, ~1 s. A 1-minute step over 30 days is 43201
// rows / 8.4 MB and *also* works, so 40000 is a conservative client-side cap,
// not an API hard limit.
static const double kHorizonsMaxRows = 40000.0;

// -- Mission catalog ---------------------------------------------------------
//
// The mission list lives entirely in the generated bundled catalog below.
// (The former 4-entry HORIZONS_MISSIONS[] "featured" array was deleted:
// nothing referenced it once the UI moved to HorizonsCatalogAt().)
//
// RULE: never derive a window from a calendar date alone. Horizons coverage
// starts and ends mid-day (measured: `-399110` coverage begins
// 1969-JUL-16 16:40:02.7475 TDB; `-399101` begins 1969-MAY-23 05:37:54.1078
// TDB, so a 16-21 May window is entirely empty because the LM had not
// separated yet; `-1024` ends 2026-APR-10 23:54:22.8576). Take t0/t1 from the
// response, never from the request.


// -- Bundled catalog --------------------------------------------------------
//
// Generated offline by scripts/gen_horizons_catalog.py and checked in; the app
// never re-queries the catalog at startup. The header is `static const` and is
// included from THIS translation unit only, so no duplicate symbols are
// possible and no runtime file or JSON parse is involved.
//
// Regenerate with:
//   python scripts/gen_horizons_catalog.py --insecure --limit 90
//       --out src/data/horizons_catalog_data.h
#include "data/horizons_catalog_data.h"

int HorizonsCatalogCount(void)
{
    return kHorizonsCatalogCount;
}

const HorizonsCatalogEntry *HorizonsCatalogAt(int index)
{
    if (index < 0 || index >= kHorizonsCatalogCount) return NULL;
    return &kHorizonsCatalog[index];
}

int HorizonsReferenceBodyCount(void)
{
    return kHorizonsReferenceBodyCount;
}

const HorizonsCatalogEntry *HorizonsReferenceBodyAt(int index)
{
    if (index < 0 || index >= kHorizonsReferenceBodyCount) return NULL;
    return &kHorizonsReferenceBodies[index];
}

// ASCII-only case folding; the generated table is ASCII by construction (the
// generator drops anything else), so this is sufficient and locale-proof.
static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return false;
    for (const char *h = haystack; *h; ++h)
    {
        const char *a = h;
        const char *b = needle;
        while (*a && *b && ascii_lower(*a) == ascii_lower(*b))
        {
            ++a;
            ++b;
        }
        if (!*b) return true;
    }
    return false;
}

int HorizonsCatalogFind(const char *query)
{
    if (!query) return -1;

    // Skip leading spaces so a pasted id still matches.
    while (*query == ' ' || *query == '\t') ++query;
    if (!*query) return -1;

    // An exact target id always wins over any name match.
    for (int i = 0; i < kHorizonsCatalogCount; ++i)
    {
        const char *cmd = kHorizonsCatalog[i].command;
        if (cmd && strcmp(cmd, query) == 0) return i;
    }

    // Otherwise a case-insensitive substring of the display name.
    for (int i = 0; i < kHorizonsCatalogCount; ++i)
    {
        if (contains_ci(kHorizonsCatalog[i].name, query)) return i;
    }
    return -1;
}

// The window used for an ad-hoc target whose caller supplied none. Horizons
// only emits a table when the requested span is fully inside coverage, and its
// data-availability horizon sits near A.D. 2100 (measured 2026-11-08). A wide
// request therefore comes back empty with a "prior to"/"after A.D." tail, and
// fetch_worker()'s single clamp-and-retry replaces the offending bound with the
// real one. That is the documented no-dead-end path for unlisted ids.
static const char *const kAdHocStart = "1600-01-01";
static const char *const kAdHocStop = "2500-01-01";
static const char *const kAdHocStep = "100 d";

bool HorizonsMakeAdHocEntry(const char *command, const char *start_time,
                            const char *stop_time, const char *step_size,
                            HorizonsMissionEntry *out)
{
    if (!out) return false;
    if (!command || !*command) return false;

    out->name = command;
    out->command = command;
    out->start_time = (start_time && *start_time) ? start_time : kAdHocStart;
    out->stop_time = (stop_time && *stop_time) ? stop_time : kAdHocStop;
    out->step_size = (step_size && *step_size) ? step_size : kAdHocStep;
    return true;
}

// -- Center body per reference frame ----------------------------------------

const char *HorizonsCenterForRef(RefBody ref)
{
    switch (ref)
    {
        case REF_BODY_MOON: return "500@301";
        case REF_BODY_SUN:  return "500@10";
        case REF_BODY_EARTH:
        default:            return "500@399";
    }
}

// -- Small time helpers (pure; also used for the row cap) --------------------

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Hinnant's algorithm).
long long days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

int month_from_abbrev(const char *p)
{
    static const char *const kNames[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    for (int i = 0; i < 12; ++i)
    {
        bool match = true;
        for (int k = 0; k < 3; ++k)
        {
            char c = p[k];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c != kNames[i][k]) { match = false; break; }
        }
        if (match) return i + 1;
    }
    return 0;
}

bool read_digits(const char *p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; ++i)
    {
        if (p[i] < '0' || p[i] > '9') return false;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return true;
}

// Parse "YYYY-MM-DD HH:MM", "YYYY-MM-DD" or "YYYY-MMM-DD HH:MM" (UTC) into
// seconds since the Unix epoch. Minute precision is enough for URL bounds.
bool parse_iso_utc_seconds(const char *s, long long *out)
{
    if (!s || !out) return false;

    int year = 0, month = 0, day = 0;
    if (!read_digits(s, 4, &year) || s[4] != '-') return false;

    const char *p = s + 5;
    if (p[0] >= '0' && p[0] <= '9')
    {
        if (!read_digits(p, 2, &month)) return false;
        p += 2;
    }
    else
    {
        month = month_from_abbrev(p);
        if (month == 0) return false;
        p += 3;
    }

    if (*p++ != '-' || !read_digits(p, 2, &day)) return false;
    p += 2;

    int hour = 0, minute = 0;
    if (*p == ' ' || *p == 'T')
    {
        ++p;
        if (!read_digits(p, 2, &hour) || p[2] != ':') return false;
        p += 3;
        if (!read_digits(p, 2, &minute)) return false;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59)
        return false;

    *out = days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL;
    return true;
}

// Parse a STEP_SIZE like "10 m", "30 s", "1 h", "1 d" into seconds.
bool parse_step_seconds(const char *s, long long *out)
{
    if (!s || !out) return false;
    long long v = 0;
    const char *p = s;
    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (*p - '0');
        ++p;
    }
    while (*p == ' ') ++p;
    char unit = *p;
    if (unit >= 'A' && unit <= 'Z') unit = (char)(unit - 'A' + 'a');

    long long mult = 0;
    switch (unit)
    {
        case 's': mult = 1; break;
        case 'm': mult = 60; break;
        case 'h': mult = 3600; break;
        case 'd': mult = 86400; break;
        default: return false;
    }
    if (v <= 0) return false;
    *out = v * mult;
    return true;
}

// Format a step expressed in seconds as the compact Horizons form.
void format_step(long long seconds, char *out, size_t out_sz)
{
    if (seconds >= 60 && seconds % 60 == 0)
        snprintf(out, out_sz, "%lld m", seconds / 60);
    else
        snprintf(out, out_sz, "%lld s", seconds);
}

} // namespace

// Keep the request size bounded by coarsening the step. Pure function: no
// state, no I/O, so it is trivially testable.
//
// Evidence for the 40000 cap: see kHorizonsMaxRows above.
double HorizonsClampStep(double span_seconds, double step_seconds)
{
    if (!(step_seconds > 0.0) || !(span_seconds > 0.0))
        return step_seconds;

    const double samples = span_seconds / step_seconds + 1.0;
    if (samples <= kHorizonsMaxRows)
        return step_seconds;

    // Spread the span over max_rows-1 intervals so the row count fits.
    return span_seconds / (kHorizonsMaxRows - 1.0);
}

// -- URL construction --------------------------------------------------------

// CENTER is pinned to Earth ('500@399') regardless of the requested reference
// body: the parser and all downstream math are geocentric, and Horizons would
// otherwise report vectors relative to a different origin and be rejected by
// the "Only Earth-centred ephemerides are supported" check in ephem_parse.cpp.
//
// REF_PLANE='FRAME' is mandatory because the parser refuses ecliptic exports
// (ephem_parse.cpp:539) and OUT_UNITS='KM-S' is mandatory because it otherwise
// applies an AU heuristic when the header does not say "KM" (:558).
//
// Duplicate query parameters are rejected by the API with HTTP 400, so each
// key below is emitted exactly once.

// Append a literal piece; fails when it would not fit (including the NUL).
static bool append_literal(char *url, size_t cap, size_t *pos, const char *text)
{
    size_t n = strlen(text);
    if (*pos + n + 1 > cap) return false;
    memcpy(url + *pos, text, n);
    *pos += n;
    url[*pos] = '\0';
    return true;
}

// Percent-encode the two characters Horizons needs escaped in a value: space
// and single quote. Everything else (digits, '-', ':', '@', ...) passes through
// unchanged, matching the regenerating recipe in testdata/README.md.
static bool append_encoded(char *url, size_t cap, size_t *pos, const char *text)
{
    for (const char *p = text; *p; ++p)
    {
        char enc[4];
        if (*p == ' ')
            snprintf(enc, sizeof(enc), "%%20");
        else if (*p == '\'')
            snprintf(enc, sizeof(enc), "%%27");
        else
        {
            enc[0] = *p;
            enc[1] = '\0';
        }
        if (!append_literal(url, cap, pos, enc)) return false;
    }
    return true;
}

// Append "&key=value" (or "?key=value" for the first param). When quote is set
// the value is wrapped in single quotes, encoded as %27.
static bool append_param(char *url, size_t cap, size_t *pos, bool *first,
                         const char *key, const char *value, bool quote)
{
    if (!append_literal(url, cap, pos, *first ? "?" : "&")) return false;
    *first = false;
    if (!append_literal(url, cap, pos, key)) return false;
    if (!append_literal(url, cap, pos, "=")) return false;
    if (quote && !append_literal(url, cap, pos, "%27")) return false;
    if (!append_encoded(url, cap, pos, value)) return false;
    if (quote && !append_literal(url, cap, pos, "%27")) return false;
    return true;
}

bool HorizonsBuildUrl(const HorizonsMissionEntry *e, char *url, size_t url_size)
{
    if (!e || !url || url_size == 0) return false;
    if (!e->command || !*e->command) return false;
    if (!e->start_time || !*e->start_time) return false;
    if (!e->stop_time || !*e->stop_time) return false;
    if (!e->step_size || !*e->step_size) return false;

    // Row cap: if the implied sample count exceeds kHorizonsMaxRows, coarsen
    // the step proportionally. The catalog strings stay free-form; only the
    // emitted query changes.
    char step_buf[32];
    const char *step = e->step_size;
    long long t0 = 0, t1 = 0, step_s = 0;
    if (parse_iso_utc_seconds(e->start_time, &t0) &&
        parse_iso_utc_seconds(e->stop_time, &t1) &&
        parse_step_seconds(e->step_size, &step_s) && t1 > t0)
    {
        const double eff = HorizonsClampStep((double)(t1 - t0), (double)step_s);
        long long eff_s = (long long)(eff + 0.5);
        if (eff_s < 1) eff_s = 1;
        if (eff_s != step_s)
        {
            format_step(eff_s, step_buf, sizeof(step_buf));
            step = step_buf;
            LOG_DEBUG("Horizons: coarsened STEP_SIZE %s -> %s (cap %.0f rows)",
                      e->step_size, step, kHorizonsMaxRows);
        }
    }

    url[0] = '\0';
    size_t pos = 0;
    if (!append_literal(url, url_size, &pos, kHorizonsBaseUrl)) return false;

    // CENTER is always Earth, never the reference body (see note above).
    static const char *const kCenter = "500@399";

    bool first = true;
    if (!append_param(url, url_size, &pos, &first, "format",     "json",  false)) return false;
    if (!append_param(url, url_size, &pos, &first, "COMMAND",    e->command,    true)) return false;
    if (!append_param(url, url_size, &pos, &first, "OBJ_DATA",   "NO",    true)) return false;
    if (!append_param(url, url_size, &pos, &first, "MAKE_EPHEM", "YES",   true)) return false;
    if (!append_param(url, url_size, &pos, &first, "EPHEM_TYPE", "VECTORS", true)) return false;
    if (!append_param(url, url_size, &pos, &first, "CENTER",     kCenter, true)) return false;
    if (!append_param(url, url_size, &pos, &first, "START_TIME", e->start_time,  true)) return false;
    if (!append_param(url, url_size, &pos, &first, "STOP_TIME",  e->stop_time,   true)) return false;
    if (!append_param(url, url_size, &pos, &first, "STEP_SIZE",  step,      true)) return false;
    if (!append_param(url, url_size, &pos, &first, "VEC_TABLE",  "2",     true)) return false;
    if (!append_param(url, url_size, &pos, &first, "OUT_UNITS",  "KM-S",  true)) return false;
    if (!append_param(url, url_size, &pos, &first, "REF_PLANE",  "FRAME", true)) return false;
    if (!append_param(url, url_size, &pos, &first, "CSV_FORMAT", "YES",   true)) return false;

    return true;
}

// -- Coverage-limit extraction ----------------------------------------------

// Horizons answers an out-of-coverage window with HTTP 200 and a tail note:
//   "No ephemeris for target ... prior to A.D. 1969-MAY-23 05:37:54.1078 TDB"
//   "No ephemeris for target ... after  A.D. 2026-APR-10 23:54:22.8576 TDB"
// Extract that datetime and normalise it to the "YYYY-MMM-DD HH:MM" form the
// API itself accepts for START_TIME/STOP_TIME. `is_prior` reports which bound
// is implicated: true means the requested START is too early, false means the
// requested STOP is too late.
bool HorizonsParseCoverageLimit(const std::string &body, bool *is_prior,
                                char *dt_iso, size_t dt_sz)
{
    if (is_prior) *is_prior = false;
    if (dt_iso && dt_sz) dt_iso[0] = '\0';

    size_t p = body.find("prior to A.D.");
    bool prior = true;
    if (p == std::string::npos)
    {
        p = body.find("after A.D.");
        prior = false;
    }
    if (p == std::string::npos) return false;

    // Scan forward for "YYYY-MMM-DD HH:MM" (seconds/fraction optional).
    const size_t n = body.size();
    for (size_t i = p; i + 17 <= n; ++i)
    {
        const char *s = body.data() + i;
        int year = 0, day = 0;
        if (!read_digits(s, 4, &year)) continue;
        if (s[4] != '-' || s[8] != '-') continue;

        const int month = month_from_abbrev(s + 5);
        if (month == 0) continue;

        if (!read_digits(s + 9, 2, &day)) continue;
        if (s[11] != ' ') continue;

        int hour = 0, minute = 0;
        if (!read_digits(s + 12, 2, &hour) || s[14] != ':') continue;
        if (!read_digits(s + 15, 2, &minute)) continue;

        if (hour > 23 || minute > 59 || day < 1 || day > 31) continue;

        // "YYYY-MMM-DD HH:MM" is 17 characters; require room for the NUL too.
        if (dt_iso && dt_sz >= 18)
        {
            memcpy(dt_iso, s, 17);
            for (int k = 5; k < 8; ++k)
            {
                char c = dt_iso[k];
                if (c >= 'a' && c <= 'z') dt_iso[k] = (char)(c - 'a' + 'A');
            }
            dt_iso[17] = '\0';
        }
        if (is_prior) *is_prior = prior;
        return true;
    }

    return false;
}

// -- Async worker state ------------------------------------------------------

namespace {

std::mutex g_mutex;                              // guards g_result / g_error
std::unique_ptr<EphemMission> g_result;          // parsed mission when DONE
std::string g_error;                             // message when FAILED
std::atomic<int> g_state{HZ_IDLE};               // HorizonsFetchState
std::atomic<long> g_http_code{0};                // last HTTP status
std::atomic<bool> g_cancel{false};               // cooperative cancel request
HorizonsMissionEntry g_pending{};                // copied catalog entry

// Copy `msg` into the caller buffer, always NUL-terminating.
void copy_error(char *err, size_t err_sz, const std::string &msg)
{
    if (!err || err_sz == 0) return;
    size_t n = msg.size();
    if (n > err_sz - 1) n = err_sz - 1;
    memcpy(err, msg.data(), n);
    err[n] = '\0';
}

// Publish a short failure and move to the terminal FAILED state.
void fail_with(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_error = msg;
    g_state.store(HZ_FAILED);
}

// Publish the terminal CANCELLED state.
void store_cancelled()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_error.clear();
    g_state.store(HZ_CANCELLED);
}

// Log the raw body at LOG_ERROR, truncated to a sane size. The user-facing
// error line stays short; this is the forensic copy. LogEntry::message is only
// 512 bytes, so emit the body in chunks to get the full ~4 KB into the ring
// buffer instead of letting vsnprintf clip one giant entry.
void log_body(const std::string &body)
{
    const size_t kMaxLog = 4096;
    const size_t kChunk = 400;
    if (body.empty())
    {
        LOG_ERROR("Horizons: response body was empty");
        return;
    }
    const size_t shown = std::min(body.size(), kMaxLog);
    LOG_ERROR("Horizons: response body (%zu bytes%s), %zu shown:",
              body.size(), (body.size() > kMaxLog) ? ", truncated" : "", shown);
    for (size_t off = 0; off < shown; off += kChunk)
    {
        const size_t n = std::min(kChunk, shown - off);
        LOG_ERROR("Horizons body[%zu]: %.*s", off, (int)n, body.data() + off);
    }
}

// A short diagnosis derived from the body. Classes measured 2026-11-08:
//   404 + nginx HTML            -> wrong host
//   400 + "API ERROR:" text     -> duplicated/garbled parameter
//   200 + "No ephemeris ..."    -> window outside coverage (handled by parser)
//   200 + small-body index text -> quotes mangled into a name search
std::string body_hint(const std::string &body)
{
    if (body.find("<title>404 Not Found</title>") != std::string::npos ||
        body.find("nginx") != std::string::npos)
        return "nginx";

    // First non-empty, non-HTML line, truncated to ~120 chars.
    size_t pos = 0;
    while (pos < body.size())
    {
        size_t eol = body.find('\n', pos);
        const size_t len = (eol == std::string::npos) ? body.size() - pos : eol - pos;
        size_t b = pos, e = pos + len;
        while (b < e && (body[b] == ' ' || body[b] == '\t' || body[b] == '\r')) ++b;
        while (e > b && (body[e - 1] == ' ' || body[e - 1] == '\t' || body[e - 1] == '\r')) --e;
        if (e > b && body[b] != '<')
        {
            std::string line = body.substr(b, std::min<size_t>(e - b, 120));
            return line;
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return "no readable body";
}

// One HTTP-or-cache request. Returns true when a body was obtained (2xx).
// On failure `fail_msg` holds the short user-facing message.
bool do_request(const std::string &url, std::string *body, long *http_code,
                std::string *fail_msg)
{
    body->clear();
    *http_code = 0;
    fail_msg->clear();

    // A cache hit still runs the parse step below; the cache holds the raw API
    // JSON, keyed on the full request URL and tagged FORMAT_HORIZONS_VECTORS.
    CacheEntry *cached = CacheGet(url.c_str());
    if (cached && cached->data && cached->data_size > 0)
    {
        body->assign(cached->data, cached->data_size);
        *http_code = 200;
        LOG_DEBUG("Horizons: cache HIT for %s", url.c_str());
        return true;
    }

    FetchResult res = HttpFetchURL(url.c_str());
    *http_code = res.http_code;
    if (res.data && res.size > 0)
        body->assign(res.data, res.size);

    const bool ok = res.success;
    if (ok)
    {
        CachePut(url.c_str(), res.data, res.size, FORMAT_HORIZONS_VECTORS);
    }
    else
    {
        char msg[192];
        if (res.http_code <= 0)
        {
            // No status line arrived; name the libcurl cause (DNS/CA/connect/timeout).
            const char *why = (res.curl_code > 0)
                                  ? curl_easy_strerror((CURLcode)res.curl_code)
                                  : "no response";
            snprintf(msg, sizeof(msg), "Horizons: network error (%s) [curl %d]",
                     why, res.curl_code);
            LOG_ERROR("Horizons: transport failed before any HTTP response: "
                      "%s (CURLcode %d) for %s",
                      why, res.curl_code, url.c_str());
        }
        else
        {
            snprintf(msg, sizeof(msg), "Horizons: HTTP %ld (%s)",
                     res.http_code, body_hint(*body).c_str());
        }
        *fail_msg = msg;
    }
    FreeFetchResult(&res);
    return ok;
}

// Unwrap {"result": "<text>"} (the $$SOE/$$EOE table is byte-identical to the
// .txt export the parser was written for). Errors come back as
// {"message": ..., "signature": ...} without a "result" field.
bool unwrap_result(const std::string &body, std::string *text, std::string *err)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(body.begin(), body.end());
    }
    catch (const std::exception &e)
    {
        *err = std::string("invalid JSON (") + e.what() + ")";
        return false;
    }

    if (!root.is_object() || !root.contains("result") || !root["result"].is_string())
    {
        if (root.is_object() && root.contains("message") && root["message"].is_string())
            *err = root["message"].get<std::string>();
        else if (root.is_object() && root.contains("signature") && root["signature"].is_string())
            *err = root["signature"].get<std::string>();
        else
            *err = "response missing result";
        return false;
    }

    *text = root["result"].get<std::string>();
    return true;
}

// Worker body: fetch (or read cache), unwrap JSON, parse, publish result.
// A single clamp-and-retry is allowed when the table is empty because the
// window falls outside the target's coverage.
void fetch_worker(HorizonsMissionEntry entry)
{
    const char *name = entry.name ? entry.name : "Horizons";

    // Cancel checkpoints are before and immediately after each request; curl
    // cannot be interrupted mid-flight from here (see HorizonsFetchCancel).
    if (g_cancel.load())
    {
        store_cancelled();
        return;
    }

    // Backing storage for a clamped window; `entry`'s const char* fields point
    // into these buffers for the lifetime of this worker.
    char start_buf[32] = {0};
    char stop_buf[32] = {0};

    char url[1024];
    if (!HorizonsBuildUrl(&entry, url, sizeof(url)))
    {
        fail_with("Horizons: failed to build request URL");
        LOG_ERROR("Horizons: failed to build request URL for %s", name);
        return;
    }

    std::string body;
    long http_code = 0;
    bool retried = false;

    for (;;)
    {
        if (g_cancel.load())
        {
            store_cancelled();
            return;
        }

        std::string fail_msg;
        const bool ok = do_request(url, &body, &http_code, &fail_msg);

        if (g_cancel.load())
        {
            store_cancelled();
            return;
        }

        g_http_code.store(http_code);

        if (!ok)
        {
            log_body(body);
            fail_with(fail_msg);
            LOG_ERROR("Horizons: %s for %s", fail_msg.c_str(), name);
            return;
        }

        std::string text;
        std::string unwrap_err;
        if (!unwrap_result(body, &text, &unwrap_err))
        {
            log_body(body);
            fail_with(unwrap_err);
            LOG_ERROR("Horizons: %s (for %s)", unwrap_err.c_str(), name);
            return;
        }

        EphemMission mission{};
        snprintf(mission.name, sizeof(mission.name), "%s", name);
        snprintf(mission.target, sizeof(mission.target), "%s", entry.command);

        char perr[256] = {0};
        if (EphemParseHorizons(text.data(), text.size(), &mission, perr, sizeof(perr)))
        {
            const int sample_count = (int)mission.s.size();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_result = std::make_unique<EphemMission>(std::move(mission));
                g_error.clear();
                g_state.store(HZ_DONE);
            }
            LOG_INFO("Horizons: fetched %d samples for %s", sample_count, name);
            return;
        }

        // Empty table: one clamp-and-retry if the tail names a coverage limit.
        // Sample times always come from the response rows, never the request.
        bool is_prior = false;
        char dt[32] = {0};
        if (!retried && HorizonsParseCoverageLimit(text, &is_prior, dt, sizeof(dt)))
        {
            retried = true;
            if (is_prior)
            {
                snprintf(start_buf, sizeof(start_buf), "%s", dt);
                entry.start_time = start_buf;
            }
            else
            {
                snprintf(stop_buf, sizeof(stop_buf), "%s", dt);
                entry.stop_time = stop_buf;
            }

            if (!HorizonsBuildUrl(&entry, url, sizeof(url)))
            {
                fail_with("Horizons: failed to build request URL");
                LOG_ERROR("Horizons: failed to build clamped URL for %s", name);
                return;
            }
            LOG_INFO("Horizons: empty table for %s; clamping %s to %s and retrying once",
                     name, is_prior ? "START_TIME" : "STOP_TIME", dt);
            continue;
        }

        // Out of coverage (or unusable table): surface the parser's tail text.
        log_body(body);
        fail_with(perr);
        LOG_ERROR("Horizons: parse failed for %s: %s", name, perr);
        return;
    }
}

} // namespace

// -- Public async API --------------------------------------------------------

bool HorizonsFetchStart(const HorizonsMissionEntry *e)
{
    if (!e) return false;
    if (g_state.load() == HZ_RUNNING) return false;

    // Copy the catalog entry by value; never retain the caller's pointer.
    g_pending = *e;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_result.reset();
        g_error.clear();
    }
    g_http_code.store(0);
    g_cancel.store(false);          // a fresh start clears any pending cancel
    g_state.store(HZ_RUNNING);

    HorizonsMissionEntry entry = g_pending;
    std::thread([entry]() { fetch_worker(entry); }).detach();
    return true;
}

void HorizonsFetchCancel(void)
{
    // Cooperative: the worker checks g_cancel before the request and right
    // after it returns. Because curl runs inside HttpFetchURL (no progress
    // callback wired in this task), a request already in flight cannot be
    // aborted mid-transfer; cancel therefore takes effect at the next
    // checkpoint. The UI stays responsive throughout because all work is
    // off-thread and the state is atomic.
    g_cancel.store(true);
}

HorizonsFetchState HorizonsFetchPoll(void)
{
    return (HorizonsFetchState)g_state.load();
}

bool HorizonsFetchTake(EphemMission *out, char *err, size_t err_sz)
{
    int state = g_state.load();
    if (state != HZ_DONE && state != HZ_FAILED && state != HZ_CANCELLED)
        return false;

    if (state == HZ_DONE)
    {
        // Success leaves the error buffer untouched: never resurrect an old
        // message here.
        std::lock_guard<std::mutex> lock(g_mutex);
        if (out && g_result)
            *out = std::move(*g_result);
        g_result.reset();
        g_error.clear();
        g_state.store(HZ_IDLE);
        return true;
    }

    if (state == HZ_CANCELLED)
    {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_error.clear();
            g_state.store(HZ_IDLE);
        }
        copy_error(err, err_sz, std::string("Cancelled."));
        return false;
    }

    // HZ_FAILED
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy_error(err, err_sz, g_error);
        g_error.clear();
        g_state.store(HZ_IDLE);
    }
    return true;
}

long HorizonsFetchHttpCode(void)
{
    return g_http_code.load();
}
