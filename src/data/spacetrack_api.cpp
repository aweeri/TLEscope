#include "data/spacetrack_api.h"
#include "core/config.h"
#include "util/log.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#endif

// -- Module state (all guarded by s_mutex) -----------------------------------

static std::mutex s_mutex;

static char s_identity[128];
static char s_password[128];
static bool s_creds_loaded = false;

static char s_cookie[512];        // "chocolatechip=...; other=..."
static time_t s_cookie_expiry = 0;

static std::chrono::steady_clock::time_point s_last_request;
static bool s_have_last_request = false;
static int s_requests_this_pull = 0;

// post-failure cooldown deadline as steady_clock ticks; 0 means no cooldown.
// Atomic so the UI can query it without taking s_mutex (which a fetch holds).
static std::atomic<long long> s_cooldown_until_ticks{0};

static char s_last_error[160];

// -- Helpers -----------------------------------------------------------------

// copy a std::string into a fixed-size char buffer (never strcpy)
static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    if (!dst || dst_size == 0) return;
    size_t n = src.size();
    if (n > dst_size - 1) n = dst_size - 1;
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// rotating static buffer in AppConfigPath: copy into a stable std::string
static std::string spacetrack_config_path(void)
{
    return std::string(AppConfigPath("spacetrack.json"));
}

// percent-encode a form field value: unreserved kept, space -> '+', others %XX
static void url_encode_form(const char *in, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p; p++)
    {
        const unsigned char c = *p;
        const bool unreserved = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            if (pos + 1 < out_size) out[pos++] = (char)c;
        }
        else if (c == ' ')
        {
            if (pos + 1 < out_size) out[pos++] = '+';
        }
        else if (pos + 3 < out_size)
        {
            out[pos++] = '%';
            out[pos++] = "0123456789ABCDEF"[c >> 4];
            out[pos++] = "0123456789ABCDEF"[c & 0xF];
        }
    }
    out[(pos < out_size) ? pos : out_size - 1] = '\0';
}

static void logout_locked(void)
{
    s_cookie[0] = '\0';
    s_cookie_expiry = 0;
}

// min 2 s between requests; the counter resets when the pacing gap implies a new pull
static void pace_request_locked(void)
{
    const auto now = std::chrono::steady_clock::now();
    if (!s_have_last_request || (now - s_last_request) > std::chrono::seconds(30))
        s_requests_this_pull = 0;

    if (s_have_last_request)
    {
        const auto elapsed = now - s_last_request;
        if (elapsed < std::chrono::seconds(2))
            std::this_thread::sleep_for(std::chrono::seconds(2) - elapsed);
    }

    // sleep out any failure cooldown before the next request goes out
    const long long until = s_cooldown_until_ticks.load();
    if (until != 0)
    {
        const auto deadline = std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(until));
        const auto now2 = std::chrono::steady_clock::now();
        if (now2 < deadline)
            std::this_thread::sleep_for(deadline - now2);
        s_cooldown_until_ticks.store(0);
    }

    s_last_request = std::chrono::steady_clock::now();
    s_have_last_request = true;
    s_requests_this_pull++;
}

// short cooldown after a failure or HTTP 429 so retries cannot hammer the API
static void arm_failure_cooldown(void)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    s_cooldown_until_ticks.store(until.time_since_epoch().count());
}

// -- Credentials -------------------------------------------------------------

// read spacetrack.json once into s_identity/s_password; never logs the contents
static void ensure_credentials_locked(void)
{
    if (s_creds_loaded) return;
    s_creds_loaded = true;

    s_identity[0] = '\0';
    s_password[0] = '\0';

    const std::string path = spacetrack_config_path();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;

    std::string text;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);

    try
    {
        nlohmann::json root = nlohmann::json::parse(text);
        copy_str(s_identity, sizeof(s_identity), root.value("identity", std::string()));
        copy_str(s_password, sizeof(s_password), root.value("password", std::string()));
    }
    catch (const std::exception &)
    {
        LOG_WARN("spacetrack.json could not be parsed");
        s_identity[0] = '\0';
        s_password[0] = '\0';
    }
}

bool SpacetrackLoadCredentials(char *identity, size_t identity_size,
                               char *password, size_t password_size)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ensure_credentials_locked();

    if (identity && identity_size) snprintf(identity, identity_size, "%s", s_identity);
    if (password && password_size) snprintf(password, password_size, "%s", s_password);

    return s_identity[0] != '\0' && s_password[0] != '\0';
}

bool SpacetrackSaveCredentials(const char *identity, const char *password)
{
    if (!identity || !identity[0] || !password || !password[0])
        return false;

    std::lock_guard<std::mutex> lock(s_mutex);

    copy_str(s_identity, sizeof(s_identity), std::string(identity));
    copy_str(s_password, sizeof(s_password), std::string(password));
    s_creds_loaded = true;

    const std::string path = spacetrack_config_path();

    nlohmann::json root;
    root["version"] = 1;
    root["identity"] = std::string(identity);
    root["password"] = std::string(password);

    FILE *f = fopen(path.c_str(), "w");
    if (!f)
    {
        LOG_ERROR("Failed to save Space-Track credentials to %s", path.c_str());
        return false;
    }
    std::string text = root.dump(2);
    fwrite(text.data(), 1, text.size(), f);
    fputc('\n', f);
    fclose(f);

#ifndef _WIN32
    chmod(path.c_str(), 0600);
#endif

    LOG_INFO("Saved Space-Track credentials");
    logout_locked();
    return true;
}

bool SpacetrackConfigured(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ensure_credentials_locked();
    return s_identity[0] != '\0' && s_password[0] != '\0';
}

// -- Session -----------------------------------------------------------------

// POST /ajaxauth/login; capture the session cookie. Caller holds s_mutex.
static bool login_locked(void)
{
    ensure_credentials_locked();
    if (!s_identity[0] || !s_password[0])
    {
        snprintf(s_last_error, sizeof(s_last_error), "not configured");
        return false;
    }

    char enc_id[256];
    char enc_pw[256];
    url_encode_form(s_identity, enc_id, sizeof(enc_id));
    url_encode_form(s_password, enc_pw, sizeof(enc_pw));

    char body[512];
    snprintf(body, sizeof(body), "identity=%s&password=%s", enc_id, enc_pw);

    HttpRequest req = {};
    req.url = "https://www.space-track.org/ajaxauth/login";
    req.post_fields = body;
    req.captured_cookie = s_cookie;
    req.captured_cookie_size = sizeof(s_cookie);
    req.keep_error_body = true;    // read the "Login" error key from the body
    req.no_retry_on_429 = true;

    pace_request_locked();
    FetchResult res = HttpPerform(&req);

    const bool ok = res.success && s_cookie[0] != '\0';

    if (ok)
    {
        s_cookie_expiry = time(NULL) + 7000;  // a little under the server's 7200
        s_last_error[0] = '\0';
        LOG_INFO("Space-Track login OK");
    }
    else
    {
        logout_locked();

        char msg[160] = {0};
        if (res.data && res.size > 0)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(res.data, res.data + res.size);
                copy_str(msg, sizeof(msg), j.value("Login", std::string()));
            }
            catch (const std::exception &) { /* not JSON: fall back to the status */ }
        }

        if (msg[0])
            snprintf(s_last_error, sizeof(s_last_error), "login failed (HTTP %ld): %.100s",
                     res.http_code, msg);
        else
            snprintf(s_last_error, sizeof(s_last_error), "login failed (HTTP %ld)", res.http_code);

        LOG_WARN("Space-Track login failed (HTTP %ld)", res.http_code);
        arm_failure_cooldown();
    }

    FreeFetchResult(&res);
    return ok;
}

bool SpacetrackLogin(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return login_locked();
}

void SpacetrackLogout(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    logout_locked();
}

bool SpacetrackSignedIn(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_cookie[0] != '\0' && time(NULL) < s_cookie_expiry;
}

// -- Presets -----------------------------------------------------------------

// slug -> GP predicate (after class/gp/); mirrors retlector_group_for_index
static const char *spacetrack_predicate_for_slug(const char *slug)
{
    static const struct { const char *slug; const char *predicate; } kPresets[] = {
        {"active",             "decay_date/null-val"},
        {"last-30-days",       "decay_date/null-val/epoch/%3Enow-30"},
        {"stations",           "decay_date/null-val/norad_cat_id/25544,48274"},
        {"starlink",           "decay_date/null-val/OBJECT_NAME/~STARLINK"},
        {"oneweb",             "decay_date/null-val/OBJECT_NAME/~ONEWEB"},
        {"gps",                "decay_date/null-val/OBJECT_NAME/~NAVSTAR"},
        {"cosmos-2251-debris", "decay_date/null-val/OBJECT_NAME/~COSMOS 2251 DEB"},
        {"fengyun-1c-debris",  "decay_date/null-val/OBJECT_NAME/~FENGYUN 1C DEB"},
        {"analyst",            "decay_date/null-val/OBJECT_TYPE/UNKNOWN"},
    };
    if (!slug) return NULL;
    for (const auto &p : kPresets)
    {
        if (strcmp(p.slug, slug) == 0)
            return p.predicate;
    }
    return NULL;
}

const char *SpacetrackPredicateForSlug(const char *slug)
{
    return spacetrack_predicate_for_slug(slug);
}

const char *SpacetrackResolvePredicate(const char *identifier, const char *custom_query)
{
    if (!identifier) return NULL;
    if (strncmp(identifier, "custom:", 7) == 0)
        return (custom_query && custom_query[0]) ? custom_query : NULL;
    return spacetrack_predicate_for_slug(identifier);
}

void SpacetrackCustomIdentifier(const char *predicate, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;

    unsigned long hash = 5381;  // djb2, same shape as cache.cpp's URL hash
    for (const unsigned char *p = (const unsigned char *)(predicate ? predicate : ""); *p; p++)
        hash = ((hash << 5) + hash) + *p;

    snprintf(out, out_size, "custom:%08lx", hash & 0xffffffffUL);
}

// -- Queries -----------------------------------------------------------------

bool SpacetrackBuildQueryUrl(const char *predicate, char *url, size_t url_size)
{
    if (!predicate || !predicate[0] || !url || url_size == 0) return false;

    static const char base[] = "https://www.space-track.org/basicspacedata/query/class/gp/";
    static const char suffix[] = "/format/json";

    size_t pos = 0;
    for (size_t i = 0; base[i]; i++)
    {
        if (pos + 1 >= url_size) return false;
        url[pos++] = base[i];
    }

    for (const unsigned char *p = (const unsigned char *)predicate; *p; p++)
    {
        const unsigned char c = *p;
        if (c == '>' || c == '<' || c == '"' || c == ' ')
        {
            if (pos + 3 >= url_size) return false;
            url[pos++] = '%';
            url[pos++] = "0123456789ABCDEF"[c >> 4];
            url[pos++] = "0123456789ABCDEF"[c & 0xF];
        }
        else
        {
            if (pos + 1 >= url_size) return false;
            url[pos++] = (char)c;
        }
    }

    for (size_t i = 0; suffix[i]; i++)
    {
        if (pos + 1 >= url_size) return false;
        url[pos++] = suffix[i];
    }

    url[pos] = '\0';
    return true;
}

FetchResult SpacetrackFetchQuery(const char *predicate)
{
    FetchResult fail = {0};
    fail.success = false;

    std::lock_guard<std::mutex> lock(s_mutex);

    if (!predicate || !predicate[0])
    {
        snprintf(s_last_error, sizeof(s_last_error), "unknown query");
        LOG_WARN("Space-Track query with no predicate");
        return fail;
    }

    // log in on demand (session cookie lasts ~7200 s)
    if (!(s_cookie[0] != '\0' && time(NULL) < s_cookie_expiry))
    {
        if (!login_locked())
            return fail;
    }

    char url[512];
    if (!SpacetrackBuildQueryUrl(predicate, url, sizeof(url)))
    {
        snprintf(s_last_error, sizeof(s_last_error), "invalid query URL");
        LOG_WARN("Space-Track: could not build query URL");
        return fail;
    }

    LOG_DEBUG("Space-Track query: %s", url);

    auto perform = [&]() -> FetchResult {
        HttpRequest req = {};
        req.url = url;
        req.cookie = s_cookie;
        req.keep_error_body = false;
        req.no_retry_on_429 = true;
        return HttpPerform(&req);
    };

    if (s_requests_this_pull >= 25)
    {
        snprintf(s_last_error, sizeof(s_last_error), "request limit reached (25/pull)");
        LOG_WARN("Space-Track: per-pull request cap reached");
        return fail;
    }

    pace_request_locked();
    FetchResult res = perform();

    // session expired: re-login once and retry the same query exactly once
    if (!res.success && res.http_code == 401)
    {
        LOG_INFO("Space-Track session expired (HTTP 401); re-logging in");
        if (login_locked() && s_requests_this_pull < 25)
        {
            pace_request_locked();
            res = perform();
        }
    }

    // rate limited: a single 5 s retry, then give up (no curl-internal retry storm)
    if (!res.success && res.http_code == 429)
    {
        LOG_WARN("Space-Track rate limited (HTTP 429); retrying in 5 s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (s_requests_this_pull < 25)
        {
            pace_request_locked();
            res = perform();
        }
    }

    if (res.success)
    {
        res.format = FORMAT_OMM_JSON;
        s_last_error[0] = '\0';
        return res;
    }

    if (res.http_code == 401)
        snprintf(s_last_error, sizeof(s_last_error), "authentication failed (HTTP 401)");
    else if (res.http_code == 429)
        snprintf(s_last_error, sizeof(s_last_error), "rate limited (HTTP 429)");
    else if (res.http_code > 0)
        snprintf(s_last_error, sizeof(s_last_error), "request failed (HTTP %ld)", res.http_code);
    else
        snprintf(s_last_error, sizeof(s_last_error), "request failed (transport)");

    LOG_WARN("Space-Track query failed: %s", s_last_error);
    arm_failure_cooldown();
    FreeFetchResult(&res);
    return fail;
}

const char *SpacetrackLastError(void)
{
    return s_last_error;
}

int SpacetrackCooldownRemainingSeconds(void)
{
    const long long until = s_cooldown_until_ticks.load();
    if (until == 0) return 0;

    const auto deadline = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(until));
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return 0;

    const long long secs =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - now).count();
    return (secs > 0) ? (int)secs : 1;
}
