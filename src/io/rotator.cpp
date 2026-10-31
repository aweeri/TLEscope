#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
typedef struct tagMSG *LPMSG;
#endif
#include "rotator.h"
#include "ui/notifications.h"
#include "core/astro.h"
#include "core/location.h"
#include "util/log.h"
#include "IconsFontAwesome6.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

typedef struct RotatorState
{
    char protocol[16];
    char host[64];
    char port[16];
    char get_fmt[64];
    char set_fmt[64];
    char custom_cmd[128];
    char park_az[16];
    char park_el[16];
    char lead_time[16];
    char los_hold_s[16];

    bool auto_steer;
    int steer_mode;

    char min_el_track[16];
    char max_el_track[16];
    char az_min[16];
    char az_max[16];
    char rate_limit_deg_s[16]; // inert, config round-trip only
    char slew_tol_deg[16];
    char slew_timeout_s[16];
    char comm_timeout_s[16];
    char send_interval_ms[16]; // transport cadence, never a target limit
    int az_wrap_mode;
    bool park_on_exit;
    bool park_between_passes;
    bool queue_tracking;

    bool connected;
    int sock;
    float cur_az;
    float cur_el;
    bool has_position;
    double last_poll_time;
    double last_send_time;
    char status[128];
} RotatorState;

static RotatorState rot = {
    "rotctld",
    "127.0.0.1",
    "4533",
    "p",
    "P %.1f %.1f",
    "",
    "180.0",
    "0.0",
    "30",
    "20",
    true,
    ROTATOR_STEER_POLAR,
    "0.0",
    "89.0",
    "0.0",
    "360.0",
    "6.0",
    "1.0",
    "60.0",
    "3.0",
    "150",
    0,
    true,
    true,
    false,
    false,             // connected
    -1,                // sock
    0.0f,              // cur_az
    0.0f,              // cur_el
    false,             // has_position
    0.0,               // last_poll_time
    0.0,               // last_send_time
    "Disconnected"     // status
};

// wall-clock guard timestamps, never the sim clock
static double mp_last_cmd_az = 0.0;
static double mp_last_cmd_el = 0.0;
static double mp_last_cmd_mono = 0.0;
static bool mp_have_last_cmd = false;
static double mp_last_poll_mono = 0.0;
static bool mp_poll_ever_ok = false;
static double mp_last_clamp_notify = 0.0;
static bool mp_comm_lost = false;
static bool mp_comm_lost_notified = false;
// latched by a manual jog so the parked re-send does not fight the operator
static bool mp_manual_override = false;

static RotatorQueue rot_queue = { {0}, 0, -1 };
static bool rot_queue_running = false;

static int RotatorQueueCmp(const void *a, const void *b)
{
    const RotatorQueueItem *x = (const RotatorQueueItem *)a;
    const RotatorQueueItem *y = (const RotatorQueueItem *)b;
    if (x->aos < y->aos) return -1;
    if (x->aos > y->aos) return 1;
    return 0;
}

static void Disconnect(void)
{
    if (rot.sock != -1)
    {
#if defined(_WIN32) || defined(_WIN64)
        closesocket((SOCKET)rot.sock);
#else
        close(rot.sock);
#endif
        rot.sock = -1;
    }
    rot.connected = false;
}

static bool ConnectTcp(const char *host, const char *port)
{
    Disconnect();
    LOG_INFO("Rotator connecting to %s:%s", host, port);

#if defined(_WIN32) || defined(_WIN64)
    static bool wsa_ready = false;
    if (!wsa_ready)
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            snprintf(rot.status, sizeof(rot.status), "WSA startup failed");
            return false;
        }
        wsa_ready = true;
    }
#endif

    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
    {
        snprintf(rot.status, sizeof(rot.status), "DNS/host lookup failed");
        return false;
    }

    int sfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sfd = (int)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0)
            continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
#if defined(_WIN32) || defined(_WIN64)
        closesocket((SOCKET)sfd);
#else
        close(sfd);
#endif
        sfd = -1;
    }
    freeaddrinfo(res);

    if (sfd < 0)
    {
        LOG_ERROR("Rotator connection failed to %s:%s", host, port);
        snprintf(rot.status, sizeof(rot.status), "Connection failed");
        NotifyPush(NOTIFY_ERROR, ICON_FA_PLUG, "Rotator connection failed");
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    DWORD timeout_ms = 1000;
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    rot.sock = sfd;
    rot.connected = true;
    snprintf(rot.status, sizeof(rot.status), "Connected to %s:%s", host, port);
    LOG_INFO("Rotator connected to %s:%s", host, port);
    NotifyPush(NOTIFY_SUCCESS, ICON_FA_PLUG, "Rotator connected");
    return true;
}

// bounded read: >0 bytes, 0 on timeout (link up), -1 on hard error
static int RecvWithTimeout(int sock, char *buf, int max_len, int timeout_ms)
{
    if (sock < 0 || !buf || max_len <= 0)
        return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET((unsigned)sock, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // select() ignores the first arg on Windows, POSIX needs sock+1
#if defined(_WIN32) || defined(_WIN64)
    int sel = select(0, &rfds, NULL, NULL, &tv);
#else
    int sel = select(sock + 1, &rfds, NULL, NULL, &tv);
#endif
    if (sel < 0)
        return -1;
    if (sel == 0)
        return 0;

#if defined(_WIN32) || defined(_WIN64)
    int n = recv((SOCKET)sock, buf, max_len - 1, 0);
#else
    int n = (int)recv(sock, buf, (size_t)(max_len - 1), 0);
#endif
    if (n > 0)
        buf[n] = '\0';
    return n;
}

static int SendLine(const char *cmd)
{
    char out[256];
    size_t cmd_len = strlen(cmd);
    if (cmd_len >= sizeof(out) - 2)
        cmd_len = sizeof(out) - 2;
    memcpy(out, cmd, cmd_len);
    if (cmd_len == 0 || out[cmd_len - 1] != '\n')
        out[cmd_len++] = '\n';
    out[cmd_len] = '\0';

#if defined(_WIN32) || defined(_WIN64)
    return send((SOCKET)rot.sock, out, (int)cmd_len, 0);
#else
    return (int)send(rot.sock, out, cmd_len, 0);
#endif
}

static bool SendLineChecked(const char *cmd)
{
    if (!rot.connected || rot.sock < 0 || !cmd || cmd[0] == '\0')
        return false;
    if (SendLine(cmd) <= 0)
    {
        snprintf(rot.status, sizeof(rot.status), "Send failed");
        Disconnect();
        return false;
    }
    return true;
}

// a read timeout here means comm slow, not a lost link
static bool SendRaw(const char *cmd, char *response, size_t response_len)
{
    if (!SendLineChecked(cmd))
        return false;

    char discard[256] = {0};
    char *resp_buf = response;
    size_t resp_len = response_len;
    if (!resp_buf || resp_len == 0)
    {
        resp_buf = discard;
        resp_len = sizeof(discard);
    }
    if (resp_len > 256) resp_len = 256;

    int n = RecvWithTimeout(rot.sock, resp_buf, (int)resp_len, 250);
    if (n < 0)
    {
        if (response && response_len > 0) response[0] = '\0';
        snprintf(rot.status, sizeof(rot.status), "Read failed");
        Disconnect();
        return false;
    }
    if (n == 0)
    {
        if (response && response_len > 0) response[0] = '\0';
        snprintf(rot.status, sizeof(rot.status), "Comm slow");
        return false;
    }
    return true;
}

// bounded drain, a silent peer must never stall the frame
static bool SendCommand(const char *cmd)
{
    if (!SendLineChecked(cmd))
        return false;

    char drain[256];
    int total_ms = 0;
    while (total_ms < 40)
    {
        int n = RecvWithTimeout(rot.sock, drain, (int)sizeof(drain), 10);
        if (n < 0)
        {
            snprintf(rot.status, sizeof(rot.status), "Read failed");
            Disconnect();
            return false;
        }
        if (n == 0)
            break;
        total_ms += 10;
    }
    return true;
}

static bool ParseFirstTwoFloats(const char *s, float *a, float *b)
{
    if (!s || !a || !b)
        return false;
    char *end = NULL;
    const char *p = s;
    float vals[2] = {0};
    int found = 0;
    while (*p && found < 2)
    {
        float v = strtof(p, &end);
        if (end != p)
        {
            vals[found++] = v;
            p = end;
        }
        else
        {
            p++;
        }
    }
    if (found < 2)
        return false;
    *a = vals[0];
    *b = vals[1];
    return true;
}

// TCP only, serial protocols go through a ser2net-style bridge
static bool DrvRotctldGetPosition(RotatorState *st, float *az, float *el)
{
    char response[256] = {0};
    if (!SendRaw(st->get_fmt, response, sizeof(response)))
        return false;
    if (!ParseFirstTwoFloats(response, az, el))
        return false;
    st->cur_az = *az;
    st->cur_el = *el;
    st->has_position = true;
    snprintf(st->status, sizeof(st->status), "OK");
    return true;
}

static bool DrvRotctldSetTarget(RotatorState *st, float az, float el)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), st->set_fmt, az, el);
    if (!SendCommand(cmd))
        return false;
    st->last_send_time = GetTime();
    return true;
}

static bool DrvRotctldStop(RotatorState *st)
{
    // best-effort, a peer without a stop command is not an error
    return SendCommand("S");
}

// GS-232 replies with the same two floats, whole degrees
static bool DrvGs232GetPosition(RotatorState *st, float *az, float *el)
{
    char response[256] = {0};
    if (!SendRaw("C2", response, sizeof(response)))
        return false;
    if (!ParseFirstTwoFloats(response, az, el))
        return false;
    st->cur_az = *az;
    st->cur_el = *el;
    st->has_position = true;
    snprintf(st->status, sizeof(st->status), "OK");
    return true;
}

static bool DrvGs232SetTarget(RotatorState *st, float az, float el)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "W%03d %03d", (int)(az + 0.5f), (int)(el + 0.5f));
    if (!SendCommand(cmd))
        return false;
    st->last_send_time = GetTime();
    return true;
}

static bool DrvGs232Stop(RotatorState *st) { return SendCommand("S"); }

// EasyComm II replies the same way
static bool DrvEasycommGetPosition(RotatorState *st, float *az, float *el)
{
    char response[256] = {0};
    if (!SendRaw("AZ EL", response, sizeof(response)))
        return false;
    if (!ParseFirstTwoFloats(response, az, el))
        return false;
    st->cur_az = *az;
    st->cur_el = *el;
    st->has_position = true;
    snprintf(st->status, sizeof(st->status), "OK");
    return true;
}

static bool DrvEasycommSetTarget(RotatorState *st, float az, float el)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AZ%.1f EL%.1f", az, el);
    if (!SendCommand(cmd))
        return false;
    st->last_send_time = GetTime();
    return true;
}

static bool DrvEasycommStop(RotatorState *st) { return SendCommand("SA SE"); }

// set_fmt for slewing, custom_cmd sent verbatim by RotatorSendCustomNow
static bool DrvCustomGetPosition(RotatorState *st, float *az, float *el)
{
    return DrvRotctldGetPosition(st, az, el);
}

static bool DrvCustomSetTarget(RotatorState *st, float az, float el)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), st->set_fmt, az, el);
    if (!SendCommand(cmd))
        return false;
    st->last_send_time = GetTime();
    return true;
}

static bool DrvCustomStop(RotatorState *st)
{
    if (st->custom_cmd[0] != '\0')
        return SendCommand(st->custom_cmd);
    return SendCommand("S");
}

static bool DrvConnect(RotatorState *st) { return ConnectTcp(st->host, st->port); }
static void DrvDisconnect(RotatorState *st) { (void)st; Disconnect(); }

// park is a one-shot slew through the same guarded path as set_target
static bool DrvRotctldPark(RotatorState *st, float az, float el) { return DrvRotctldSetTarget(st, az, el); }
static bool DrvGs232Park(RotatorState *st, float az, float el)    { return DrvGs232SetTarget(st, az, el); }
static bool DrvEasycommPark(RotatorState *st, float az, float el) { return DrvEasycommSetTarget(st, az, el); }
static bool DrvCustomPark(RotatorState *st, float az, float el)   { return DrvCustomSetTarget(st, az, el); }

// table order is the UI dropdown order, index 0 is the default
static const RotatorDriver g_rotator_drivers[] = {
    { "rotctld",  "rotctld / generic (Hamlib)", true,
      DrvConnect, DrvDisconnect, DrvRotctldGetPosition, DrvRotctldSetTarget, DrvRotctldPark, DrvRotctldStop },
    { "gs232",    "GS-232 (Yaesu-style)", false,
      DrvConnect, DrvDisconnect, DrvGs232GetPosition, DrvGs232SetTarget, DrvGs232Park, DrvGs232Stop },
    { "easycomm", "EasyComm II", true,
      DrvConnect, DrvDisconnect, DrvEasycommGetPosition, DrvEasycommSetTarget, DrvEasycommPark, DrvEasycommStop },
    { "custom",   "Custom command", true,
      DrvConnect, DrvDisconnect, DrvCustomGetPosition, DrvCustomSetTarget, DrvCustomPark, DrvCustomStop },
};
static const int g_rotator_driver_count =
    (int)(sizeof(g_rotator_drivers) / sizeof(g_rotator_drivers[0]));

// resolved at connect and on load, rotctld is the unknown-key fallback
static const RotatorDriver *g_rotator_driver = &g_rotator_drivers[0];

int RotatorProtocolCount(void) { return g_rotator_driver_count; }
const char *RotatorProtocolKey(int idx)
{
    return (idx >= 0 && idx < g_rotator_driver_count) ? g_rotator_drivers[idx].key : NULL;
}
const char *RotatorProtocolLabel(int idx)
{
    return (idx >= 0 && idx < g_rotator_driver_count) ? g_rotator_drivers[idx].label : NULL;
}
const char *RotatorGetProtocol(void)
{
    return rot.protocol[0] ? rot.protocol : "rotctld";
}

static const RotatorDriver *SelectDriver(const char *key)
{
    if (key && key[0])
    {
        for (int i = 0; i < g_rotator_driver_count; i++)
            if (strcmp(g_rotator_drivers[i].key, key) == 0)
                return &g_rotator_drivers[i];
        LOG_WARN("Rotator: unknown protocol '%s', falling back to rotctld", key);
        NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                   "Unknown rotator protocol '%s'; using rotctld", key);
    }
    return &g_rotator_drivers[0];
}

static void RefreshDriver(void)
{
    g_rotator_driver = SelectDriver(rot.protocol);
    LOG_INFO("Rotator protocol: %s", g_rotator_driver->key);
}

static void PollPosition(void)
{
    if (!rot.connected)
        return;
    // poll cadence is separate from transmit cadence, keeps the watchdog fed
    if (GetTime() - rot.last_poll_time < 0.5)
        return;
    rot.last_poll_time = GetTime();

    float az = 0.0f, el = 0.0f;
    if (g_rotator_driver->get_position(&rot, &az, &el))
    {
        LOG_DEBUG("Rotator position: AZ=%.1f EL=%.1f", rot.cur_az, rot.cur_el);
        // watchdog uses the real wall clock, never the sim clock
        mp_last_poll_mono = GetTime();
        mp_poll_ever_ok = true;
    }
    else if (rot.connected)
    {
        // SendRaw already dropped the link on hard errors, these are parse failures
        if (strcmp(rot.status, "Comm slow") != 0)
            snprintf(rot.status, sizeof(rot.status), "Parse failed");
    }
}

static bool SetPosition(float az, float el)
{
    if (!rot.connected)
        return false;
    return g_rotator_driver->set_target(&rot, az, el);
}

static float RotCfgF(const char *s, float def)
{
    if (!s || s[0] == '\0')
        return def;
    char *end = NULL;
    float v = strtof(s, &end);
    return (end == s) ? def : v;
}

// transmission cadence only, never scales or smooths the target
static double RotSendIntervalSec(void)
{
    float ms = RotCfgF(rot.send_interval_ms, 150.0f);
    if (ms < 0.0f) ms = 0.0f;
    if (ms > 5000.0f) ms = 5000.0f;
    return (double)ms / 1000.0;
}

static float RotClampF(float v, float lo, float hi)
{
    if (lo > hi) { float t = lo; lo = hi; hi = t; }
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float RotNorm360(float a)
{
    a = fmodf(a, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a;
}

// continuity baseline, dropped on connect/disconnect/jog so it re-seeds
static double mp_az_cont = 0.0;
static bool mp_have_az_cont = false;

static void RotResetAzBaseline(void) { mp_have_az_cont = false; }

// 359.9 -> 0.1 must be +0.2, not -359.8: pick the turn nearest the baseline,
// which is the last command, the position or the target
static double RotResolveAzContinuous(float az_norm)
{
    double base;
    if (mp_have_az_cont)
        base = mp_az_cont;
    else if (rot.has_position)
        base = (double)RotNorm360(rot.cur_az);
    else
        return (double)az_norm;

    switch (rot.az_wrap_mode)
    {
    case 1:
        return base + (double)RotNorm360(az_norm - (float)base);
    case 2:
        return base - (double)RotNorm360((float)base - az_norm);
    default:
    {
        double d = (double)az_norm - base;
        while (d > 180.0)  d -= 360.0;
        while (d < -180.0) d += 360.0;
        return base + d;
    }
    }
}

// wrap-crossing windows (350..10) are legal and keep their turn offset
static double RotClampAzToWindow(double cand, float lo, float hi)
{
    float span = RotNorm360(hi - lo);
    if (span <= 0.0f || span >= 360.0f)
        return cand;

    float rel = RotNorm360(RotNorm360((float)cand) - lo);
    if (rel <= span)
        return cand;

    float d_lo = 360.0f - rel;
    float d_hi = rel - span;
    double e = (d_lo <= d_hi) ? (double)lo : (double)RotNorm360(lo + span);
    double k = floor((cand - e) / 360.0 + 0.5);
    return e + 360.0 * k;
}

// one turn of headroom each way, keeps the north-crossing hint
static double RotFoldAzContinuous(double a)
{
    while (a >= 720.0)  a -= 360.0;
    while (a < -360.0)  a += 360.0;
    return a;
}

// 360 is the north alias on an upward crossing for integer protocols
static double RotWireAzInteger(double az_cont)
{
    double a = (double)RotNorm360((float)az_cont);
    int r = (int)floor(a + 0.5);
    if (r >= 360)
        return (az_cont < 0.0) ? 0.0 : 360.0;
    if (r == 0 && mp_have_last_cmd && (az_cont - a) > 0.0)
        return 360.0;
    return (double)r;
}

// single azimuth pipeline: normalise, unwrap, clamp, fold, wire
static float RotatorWireAzimuth(float az, double *cont_out)
{
    double cont = RotResolveAzContinuous(RotNorm360(az));
    cont = RotClampAzToWindow(cont, RotCfgF(rot.az_min, 0.0f),
                              RotCfgF(rot.az_max, 360.0f));
    cont = RotFoldAzContinuous(cont);
    *cont_out = cont;
    return g_rotator_driver->az_continuous ? (float)cont : (float)RotWireAzInteger(cont);
}

// the only motion funnel, dedup skips identical resends, force_now skips cadence
static void RotatorCommandSlew(float az, float el, bool dedup_identical = true,
                               bool force_now = false)
{
    if (!rot.connected)
        return;

    double now_mono = GetTime();

    el = RotClampF(el, -90.0f, 90.0f);
    float el_limited = RotClampF(el, RotCfgF(rot.min_el_track, 0.0f),
                                 RotCfgF(rot.max_el_track, 89.0f));
    if (el_limited != el && (now_mono - mp_last_clamp_notify) >= 1.0)
    {
        mp_last_clamp_notify = now_mono;
        NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                   "Rotator elevation clamped to limits");
    }
    el = el_limited;

    double az_cont = 0.0;
    float wire_az = RotatorWireAzimuth(az, &az_cont);

    if (dedup_identical && mp_have_last_cmd &&
        wire_az == (float)mp_last_cmd_az && el == (float)mp_last_cmd_el)
        return;

    // cadence paces the wire, the value itself is untouched
    if (!force_now && mp_have_last_cmd &&
        (now_mono - mp_last_cmd_mono) < RotSendIntervalSec())
        return;

    if (SetPosition(wire_az, el))
    {
        mp_last_cmd_az = wire_az;
        mp_last_cmd_el = el;
        mp_last_cmd_mono = now_mono;
        mp_have_last_cmd = true;
        mp_az_cont = az_cont;
        mp_have_az_cont = true;
    }
}

void RotatorShutdown(void)
{
    Disconnect();
    // reset guard state so a reconnect starts cleanly
    mp_have_last_cmd = false;
    RotResetAzBaseline();
    mp_poll_ever_ok = false;
    mp_comm_lost = false;
    mp_comm_lost_notified = false;
}

void RotatorSaveSettings(AppConfig *cfg)
{
    if (!cfg) return;
    RotatorSettings *R = &cfg->rotator_settings;
    strncpy(R->protocol, rot.protocol, sizeof(R->protocol) - 1);
    strncpy(R->host, rot.host, sizeof(R->host) - 1);
    strncpy(R->port, rot.port, sizeof(R->port) - 1);
    strncpy(R->get_fmt, rot.get_fmt, sizeof(R->get_fmt) - 1);
    strncpy(R->set_fmt, rot.set_fmt, sizeof(R->set_fmt) - 1);
    strncpy(R->custom_cmd, rot.custom_cmd, sizeof(R->custom_cmd) - 1);
    strncpy(R->park_az, rot.park_az, sizeof(R->park_az) - 1);
    strncpy(R->park_el, rot.park_el, sizeof(R->park_el) - 1);
    strncpy(R->lead_time, rot.lead_time, sizeof(R->lead_time) - 1);
    strncpy(R->los_hold_s, rot.los_hold_s, sizeof(R->los_hold_s) - 1);
    R->auto_steer = rot.auto_steer;
    R->steer_mode = rot.steer_mode;
    strncpy(R->min_el_track, rot.min_el_track, sizeof(R->min_el_track) - 1);
    strncpy(R->max_el_track, rot.max_el_track, sizeof(R->max_el_track) - 1);
    strncpy(R->az_min, rot.az_min, sizeof(R->az_min) - 1);
    strncpy(R->az_max, rot.az_max, sizeof(R->az_max) - 1);
    strncpy(R->rate_limit_deg_s, rot.rate_limit_deg_s, sizeof(R->rate_limit_deg_s) - 1);
    strncpy(R->slew_tol_deg, rot.slew_tol_deg, sizeof(R->slew_tol_deg) - 1);
    strncpy(R->slew_timeout_s, rot.slew_timeout_s, sizeof(R->slew_timeout_s) - 1);
    strncpy(R->comm_timeout_s, rot.comm_timeout_s, sizeof(R->comm_timeout_s) - 1);
    strncpy(R->send_interval_ms, rot.send_interval_ms, sizeof(R->send_interval_ms) - 1);
    R->az_wrap_mode = rot.az_wrap_mode;
    R->park_on_exit = rot.park_on_exit;
    R->park_between_passes = rot.park_between_passes;
    R->queue_tracking = rot.queue_tracking;
}

void RotatorLoadSettings(const AppConfig *cfg)
{
    if (!cfg) return;
    const RotatorSettings *R = &cfg->rotator_settings;
    // a config without a protocol key keeps the rotctld default
    if (R->protocol[0])
    {
        strncpy(rot.protocol, R->protocol, sizeof(rot.protocol) - 1);
        rot.protocol[sizeof(rot.protocol) - 1] = '\0';
    }
    strncpy(rot.host, R->host, sizeof(rot.host) - 1);
    strncpy(rot.port, R->port, sizeof(rot.port) - 1);
    strncpy(rot.get_fmt, R->get_fmt, sizeof(rot.get_fmt) - 1);
    strncpy(rot.set_fmt, R->set_fmt, sizeof(rot.set_fmt) - 1);
    strncpy(rot.custom_cmd, R->custom_cmd, sizeof(rot.custom_cmd) - 1);
    strncpy(rot.park_az, R->park_az, sizeof(rot.park_az) - 1);
    strncpy(rot.park_el, R->park_el, sizeof(rot.park_el) - 1);
    strncpy(rot.lead_time, R->lead_time, sizeof(rot.lead_time) - 1);
    strncpy(rot.los_hold_s, R->los_hold_s, sizeof(rot.los_hold_s) - 1);
    rot.auto_steer = R->auto_steer;
    rot.steer_mode = R->steer_mode;
    strncpy(rot.min_el_track, R->min_el_track, sizeof(rot.min_el_track) - 1);
    strncpy(rot.max_el_track, R->max_el_track, sizeof(rot.max_el_track) - 1);
    strncpy(rot.az_min, R->az_min, sizeof(rot.az_min) - 1);
    strncpy(rot.az_max, R->az_max, sizeof(rot.az_max) - 1);
    strncpy(rot.rate_limit_deg_s, R->rate_limit_deg_s, sizeof(rot.rate_limit_deg_s) - 1);
    strncpy(rot.slew_tol_deg, R->slew_tol_deg, sizeof(rot.slew_tol_deg) - 1);
    strncpy(rot.slew_timeout_s, R->slew_timeout_s, sizeof(rot.slew_timeout_s) - 1);
    strncpy(rot.comm_timeout_s, R->comm_timeout_s, sizeof(rot.comm_timeout_s) - 1);
    strncpy(rot.send_interval_ms, R->send_interval_ms, sizeof(rot.send_interval_ms) - 1);
    rot.az_wrap_mode = R->az_wrap_mode;
    rot.park_on_exit = R->park_on_exit;
    rot.park_between_passes = R->park_between_passes;
    rot.queue_tracking = R->queue_tracking;
    // only the first load sets the run gate, later edits must not clobber Start/Stop
    static bool run_state_restored = false;
    if (!run_state_restored)
    {
        rot_queue_running = R->queue_tracking;
        run_state_restored = true;
    }
}

int RotatorGetHostBufferSize(void) { return (int)sizeof(rot.host); }
int RotatorGetPortBufferSize(void) { return (int)sizeof(rot.port); }
int RotatorGetGetFmtBufferSize(void) { return (int)sizeof(rot.get_fmt); }
int RotatorGetSetFmtBufferSize(void) { return (int)sizeof(rot.set_fmt); }
int RotatorGetCustomCmdBufferSize(void) { return (int)sizeof(rot.custom_cmd); }
int RotatorGetParkAzBufferSize(void) { return (int)sizeof(rot.park_az); }
int RotatorGetParkElBufferSize(void) { return (int)sizeof(rot.park_el); }
int RotatorGetLeadTimeBufferSize(void) { return (int)sizeof(rot.lead_time); }
int RotatorGetLosHoldBufferSize(void) { return (int)sizeof(rot.los_hold_s); }

bool RotatorGetAutoSteer(void) { return rot.auto_steer; }
void RotatorSetAutoSteer(bool enabled) { rot.auto_steer = enabled; mp_manual_override = false; }
int RotatorGetLeadTimeSec(void) { return (int)atol(rot.lead_time); }
int RotatorGetLosHoldSec(void) { return (int)atol(rot.los_hold_s); }
bool RotatorGetParkBetweenPasses(void) { return rot.park_between_passes; }
bool RotatorGetQueueTracking(void) { return rot.queue_tracking; }
void RotatorConnect(void)
{
    RefreshDriver();
    g_rotator_driver->connect(&rot);
    // fresh session, drop last command and baseline so the next seed is from hardware
    mp_have_last_cmd = false;
    RotResetAzBaseline();
    mp_poll_ever_ok = false;
    mp_last_poll_mono = GetTime();
    mp_comm_lost = false;
    mp_comm_lost_notified = false;
}
void RotatorDisconnect(void)
{
    LOG_INFO("Rotator shutting down");
    Disconnect();
    RotResetAzBaseline();
    snprintf(rot.status, sizeof(rot.status), "Disconnected");
    NotifyPush(NOTIFY_INFO, ICON_FA_PLUG, "Rotator disconnected");
}
void RotatorPollNow(void) { PollPosition(); }
void RotatorSendCustomNow(void)
{
    if (rot.custom_cmd[0] != '\0')
        SendCommand(rot.custom_cmd);
}

// park re-sends are idempotent, paced by the funnel cadence
static void RotatorSlewToPark(void)
{
    if (!rot.connected)
        return;
    RotatorCommandSlew(RotCfgF(rot.park_az, 180.0f), RotCfgF(rot.park_el, 0.0f), false);
}

void RotatorParkNow(void)
{
    if (!rot.connected)
        return;
    LOG_INFO("Rotator parking to AZ=%.1f EL=%.1f",
             RotCfgF(rot.park_az, 180.0f), RotCfgF(rot.park_el, 0.0f));
    RotatorSlewToPark();
}

void RotatorQueueClear(void)
{
    rot_queue.count = 0;
    rot_queue.active_idx = -1;
}

bool RotatorQueueIsQueued(uint32_t norad, double aos)
{
    for (int i = 0; i < rot_queue.count; i++)
        if (rot_queue.items[i].norad == norad && fabs(rot_queue.items[i].aos - aos) < 1e-3)
            return true;
    return false;
}

bool RotatorQueueToggle(uint32_t norad, double aos, double los, float max_el)
{
    for (int i = 0; i < rot_queue.count; i++)
    {
        if (rot_queue.items[i].norad == norad && fabs(rot_queue.items[i].aos - aos) < 1e-3)
        {
            for (int j = i; j + 1 < rot_queue.count; j++)
                rot_queue.items[j] = rot_queue.items[j + 1];
            rot_queue.count--;
            // keep the active pointer sensible after the shift
            if (rot_queue.active_idx == i) rot_queue.active_idx = -1;
            else if (rot_queue.active_idx > i) rot_queue.active_idx--;
            return false;
        }
    }

    if (rot_queue.count >= MAX_ROTATOR_QUEUE)
        return false;

    RotatorQueueItem item;
    memset(&item, 0, sizeof(item));
    item.norad = norad;
    item.aos = aos;
    item.los = los;
    item.max_el = max_el;
    item.max_el_epoch = 0.0; // refined by RotatorQueueResolve()
    item.state = RQ_PENDING;
    item.armed = true;
    item.tracked = false;
    item.arrived = false;
    rot_queue.items[rot_queue.count++] = item;

    qsort(rot_queue.items, (size_t)rot_queue.count, sizeof(RotatorQueueItem), RotatorQueueCmp);
    return true;
}

int RotatorQueueCount(void) { return rot_queue.count; }

const RotatorQueueItem *RotatorQueueItems(void)
{
    return rot_queue.count > 0 ? rot_queue.items : NULL;
}

int RotatorQueueActiveIndex(void) { return rot_queue.active_idx; }

// positive listing, an unknown state from a newer build must not read as terminal
static bool RotatorStateIsTerminal(int st)
{
    return st == RQ_DONE || st == RQ_SKIPPED || st == 4; // 4 = retired RQ_FAILED
}

void RotatorSerializeQueue(nlohmann::json &out)
{
    out = nlohmann::json::array();
    for (int i = 0; i < rot_queue.count; i++)
    {
        const RotatorQueueItem *it = &rot_queue.items[i];
        nlohmann::json j;
        j["norad"] = it->norad;
        j["aos"] = it->aos;
        j["los"] = it->los;
        j["max_el"] = it->max_el;
        j["max_el_epoch"] = it->max_el_epoch;
        j["state"] = (int)it->state;
        j["armed"] = it->armed;
        j["los_az"] = it->los_az;
        j["los_el"] = it->los_el;
        out.push_back(j);
    }
}

void RotatorDecodeQueue(const nlohmann::json &in)
{
    RotatorQueueClear();
    if (!in.is_array())
        return; // missing or wrong-typed block, stay empty

    for (const nlohmann::json &j : in)
    {
        if (!j.is_object())
            continue;

        const nlohmann::json &jn = j["norad"];
        const nlohmann::json &ja = j["aos"];
        if (!jn.is_number() || !ja.is_number())
            continue;
        uint32_t norad = (uint32_t)jn.get<double>();
        double aos = ja.get<double>();
        if (norad == 0u || !(aos > 0.0))
            continue;

        int st = j["state"].is_number_integer() ? j["state"].get<int>() : (int)RQ_PENDING;
        if (RotatorStateIsTerminal(st))
            continue;

        if (rot_queue.count >= MAX_ROTATOR_QUEUE)
            break;

        RotatorQueueItem it;
        memset(&it, 0, sizeof(it));
        it.norad = norad;
        it.aos = aos;
        it.los = j["los"].is_number() ? j["los"].get<double>() : 0.0;
        it.max_el = j["max_el"].is_number() ? (float)j["max_el"].get<double>() : 0.0f;
        it.max_el_epoch = j["max_el_epoch"].is_number() ? j["max_el_epoch"].get<double>() : 0.0;
        it.state = RQ_PENDING;
        it.armed = j["armed"].is_boolean() ? j["armed"].get<bool>() : true;
        it.tracked = false;
        it.arrived = false;
        it.los_az = j["los_az"].is_number() ? (float)j["los_az"].get<double>() : 0.0f;
        it.los_el = j["los_el"].is_number() ? (float)j["los_el"].get<double>() : 0.0f;
        rot_queue.items[rot_queue.count++] = it;
    }

    rot_queue.active_idx = -1;
    if (rot_queue.count > 0)
        qsort(rot_queue.items, (size_t)rot_queue.count, sizeof(RotatorQueueItem), RotatorQueueCmp);
}

// exact identity is 1e-3 day, the wider window absorbs a re-prediction
// LEO passes are ~90 min apart, so 15 min cannot mis-bind
#define ROT_AOS_MATCH_DAYS (1e-3)
#define ROT_AOS_DRIFT_DAYS (15.0 / 1440.0)

// shared by RotatorQueueResolve and RotatorTick so both bind the same pass
static const struct SatPass *RotatorMatchPass(const struct SatPass *passes_in,
                                              int pass_count, uint32_t norad,
                                              double aos)
{
    const struct SatPass *best = NULL;
    double best_d = 0.0;
    for (int i = 0; i < pass_count; i++)
    {
        if (!passes_in[i].sat || passes_in[i].sat->norad_id_num != norad)
            continue;
        double d = fabs(passes_in[i].aos_epoch - aos);
        if (d < ROT_AOS_MATCH_DAYS)
            return &passes_in[i];
        if (d <= ROT_AOS_DRIFT_DAYS && (!best || d < best_d))
        {
            best = &passes_in[i];
            best_d = d;
        }
    }
    return best;
}

// the only place the phase clock is armed, on real state changes only,
// so time-warp cannot expire the slew timeout instantly
static void RotatorPhaseEnter(RotatorQueueItem *item, RotatorQueueState st, double now)
{
    if (!item)
        return;
    if (item->state != st)
        item->phase_start_epoch = now;
    item->state = st;
}

int RotatorQueueResolve(const struct SatPass *passes_in, int pass_count, double now)
{
    // empty pass list means predictions are not ready, do not prune
    if (pass_count <= 0)
        return rot_queue.count;

    // keep the active identity so the pointer survives the rebuild
    uint32_t act_norad = 0;
    double act_aos = 0.0;
    bool had_active = (rot_queue.active_idx >= 0 && rot_queue.active_idx < rot_queue.count);
    if (had_active)
    {
        act_norad = rot_queue.items[rot_queue.active_idx].norad;
        act_aos = rot_queue.items[rot_queue.active_idx].aos;
    }

    RotatorQueueItem kept[MAX_ROTATOR_QUEUE];
    int kept_n = 0;
    double act_new_aos = act_aos;
    for (int q = 0; q < rot_queue.count; q++)
    {
        bool is_active_item = had_active && rot_queue.items[q].norad == act_norad &&
                              fabs(rot_queue.items[q].aos - act_aos) < 1e-3;

        const struct SatPass *match = RotatorMatchPass(passes_in, pass_count,
                                                       rot_queue.items[q].norad,
                                                       rot_queue.items[q].aos);

        // never drop the active item mid-pass, RotatorRebindPass re-binds it later
        bool in_window = (rot_queue.items[q].los > 0.0 &&
                          now >= rot_queue.items[q].aos &&
                          now <= rot_queue.items[q].los);
        if (!match && is_active_item && in_window)
        {
            kept[kept_n++] = rot_queue.items[q];
            continue;
        }

        if (!match)
            continue;

        RotatorQueueItem it = rot_queue.items[q];
        it.aos = match->aos_epoch;
        it.los = match->los_epoch;
        it.max_el = match->max_el;
        it.max_el_epoch = match->max_el_epoch;
        if (is_active_item)
            act_new_aos = it.aos;
        kept[kept_n++] = it;
    }

    memcpy(rot_queue.items, kept, sizeof(RotatorQueueItem) * (size_t)kept_n);
    rot_queue.count = kept_n;

    if (kept_n > 0)
        qsort(rot_queue.items, (size_t)kept_n, sizeof(RotatorQueueItem), RotatorQueueCmp);

    // re-point the active item at its (possibly moved) slot
    rot_queue.active_idx = -1;
    if (had_active)
    {
        for (int i = 0; i < rot_queue.count; i++)
        {
            if (rot_queue.items[i].norad == act_norad &&
                fabs(rot_queue.items[i].aos - act_new_aos) < 1e-3)
            {
                rot_queue.active_idx = i;
                break;
            }
        }
    }
    return rot_queue.count;
}

void RotatorQueueStart(void)
{
    if (!rot_queue_running)
        NotifyPush(NOTIFY_INFO, ICON_FA_PLAY, "Rotator queue started (%d pass%s)",
                   rot_queue.count, rot_queue.count == 1 ? "" : "es");
    rot_queue_running = true;
    rot.queue_tracking = true;
    mp_manual_override = false;
}
void RotatorQueueStop(void)
{
    bool was_running = rot_queue_running;
    if (was_running && rot.connected && rot.park_on_exit)
        RotatorParkNow();
    rot_queue_running = false;
    rot.queue_tracking = false;
    mp_manual_override = false;
    // close a post-LOS hold so a stop never leaves a stale state
    if (rot_queue.active_idx >= 0 && rot_queue.active_idx < rot_queue.count &&
        rot_queue.items[rot_queue.active_idx].state == RQ_LOS_HOLD)
    {
        rot_queue.items[rot_queue.active_idx].state = RQ_DONE;
        rot_queue.active_idx = -1;
    }
    if (was_running)
        NotifyPush(NOTIFY_INFO, ICON_FA_STOP, "Rotator queue stopped");
}

int RotatorQueueAdvance(int current_idx, double now)
{
    int start = (current_idx >= 0) ? current_idx + 1 : 0;
    for (int i = start; i < rot_queue.count; i++)
    {
        if (!(rot_queue.items[i].armed && rot_queue.items[i].state == RQ_PENDING))
            continue;

        // skip elapsed windows so the queue never stalls at the horizon
        if (rot_queue.items[i].los > 0.0 && now > rot_queue.items[i].los)
        {
            rot_queue.items[i].state = RQ_SKIPPED;
            continue;
        }

        rot_queue.items[i].tracked = false;
        rot_queue.items[i].arrived = false;
        rot_queue.items[i].los_az = 0.0f;
        rot_queue.items[i].los_el = 0.0f;
        RotatorPhaseEnter(&rot_queue.items[i], RQ_AWAITING, now);
        rot_queue.active_idx = i;
        return i;
    }
    rot_queue.active_idx = -1;
    return -1;
}

static float RotAngSep(float a, float b)
{
    float d = RotNorm360(a - b);
    if (d > 180.0f) d = 360.0f - d;
    return d;
}

static void RotatorAzElAtEpoch(const struct SatPass *p, double epoch,
                               float *az_out, float *el_out)
{
    double gmst = epoch_to_gmst(epoch);
    Vector3 pos = calculate_position(p->sat, get_unix_from_epoch(epoch));
    double az = 0.0, el = 0.0;
    get_az_el(pos, gmst, GetHomeLocation()->lat, GetHomeLocation()->lon,
              GetHomeLocation()->alt, &az, &el);
    if (az_out) *az_out = (float)az;
    if (el_out) *el_out = (float)el;
}

static void RotatorTickPark(void)
{
    if (!mp_manual_override)
        RotatorSlewToPark();
}

// states that own the active pointer and are cancelled when tracking stops
static bool RotatorInProgress(RotatorQueueState st)
{
    return st == RQ_ACTIVE || st == RQ_AWAITING ||
           st == RQ_AWAITING_AOS || st == RQ_LOS_HOLD;
}

static RotatorQueueItem *RotatorActiveItem(void)
{
    if (rot_queue.active_idx < 0 || rot_queue.active_idx >= rot_queue.count)
        return NULL;
    return &rot_queue.items[rot_queue.active_idx];
}

static void RotatorReleaseActive(void)
{
    RotatorQueueItem *it = RotatorActiveItem();
    if (it && RotatorInProgress(it->state))
        rot_queue.active_idx = -1;
}

static double RotatorHoldEndEpoch(double los_epoch)
{
    int hold_s = RotatorGetLosHoldSec();
    if (hold_s < 0) hold_s = 0;
    return los_epoch + (double)hold_s / 86400.0;
}

static void RotatorFinishAndAdvance(RotatorQueueItem *item, RotatorQueueState st,
                                    double now)
{
    RotatorPhaseEnter(item, st, now);
    RotatorQueueAdvance(rot_queue.active_idx, now);
    if (rot.park_between_passes)
        RotatorTickPark();
}

// re-bind a tracked item to its live pass after a re-prediction moved its AOS
static const struct SatPass *RotatorRebindPass(RotatorQueueItem *item,
                                               const struct SatPass *passes_in,
                                               int pass_count, double now)
{
    const struct SatPass *nearest = NULL;
    double nearest_d = 0.0;
    for (int i = 0; i < pass_count; i++)
    {
        if (!passes_in[i].sat || passes_in[i].sat->norad_id_num != item->norad)
            continue;
        if (now < passes_in[i].aos_epoch || now > passes_in[i].los_epoch)
            continue;
        double d = fabs(passes_in[i].aos_epoch - item->aos);
        if (d > ROT_AOS_DRIFT_DAYS)
            continue;
        if (!nearest || d < nearest_d)
        {
            nearest = &passes_in[i];
            nearest_d = d;
        }
    }
    if (nearest)
    {
        item->aos = nearest->aos_epoch;
        item->los = nearest->los_epoch;
        item->max_el = nearest->max_el;
        item->max_el_epoch = nearest->max_el_epoch;
    }
    return nearest;
}

// drive one target, the slew timeout only applies before AOS
static void RotatorTrackTarget(RotatorQueueItem *item, float target_az, float target_el,
                               double now, bool before_los)
{
    item->tracked = true;

    float tol = RotCfgF(rot.slew_tol_deg, 1.0f);
    if (rot.has_position && RotAngSep(rot.cur_az, target_az) <= tol &&
        fabsf(rot.cur_el - target_el) <= tol)
        item->arrived = true;

    float stimeout = RotCfgF(rot.slew_timeout_s, 60.0f);
    bool pre_aos_aim = before_los && now < item->aos;
    if (pre_aos_aim && !item->arrived && rot.has_position && stimeout > 0.0f &&
        item->phase_start_epoch > 0.0 &&
        (now - item->phase_start_epoch) * 86400.0 > (double)stimeout)
    {
        LOG_DEBUG("Rotator pre-AOS slew timeout: norad=%u elapsed=%.0fs, skipping",
                  item->norad, (now - item->phase_start_epoch) * 86400.0);
        RotatorFinishAndAdvance(item, RQ_SKIPPED, now);
        return;
    }

    RotatorCommandSlew(target_az, target_el);
}

// now is the sim clock for pass timing only, the comm watchdog uses wall clock
// guard order: poll, autosteer, comm lost, queue stopped, advance, LOS, pre-AOS
void RotatorTick(const struct SatPass *passes_in, int pass_count, double now)
{
    static bool was_steering = false;

    if (rot.connected)
        PollPosition();

    bool tracking_active = rot.auto_steer && rot_queue_running &&
                           rot.steer_mode == ROTATOR_STEER_POLAR;
    if (was_steering && !tracking_active)
        RotatorReleaseActive();
    was_steering = tracking_active;

    if (!rot.auto_steer)
    {
        if (rot.park_on_exit)
            RotatorTickPark();
        return;
    }

    double comm_timeout = (double)RotCfgF(rot.comm_timeout_s, 3.0f);
    if (comm_timeout < 0.1) comm_timeout = 0.1;
    if (mp_poll_ever_ok && (GetTime() - mp_last_poll_mono) > comm_timeout)
    {
        if (!mp_comm_lost_notified)
        {
            mp_comm_lost_notified = true;
            NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION,
                       "Rotator comm lost (no position update)");
        }
        mp_comm_lost = true;
        RotatorReleaseActive();
        return;
    }
    if (mp_comm_lost && (GetTime() - mp_last_poll_mono) <= comm_timeout)
    {
        mp_comm_lost = false;
        mp_comm_lost_notified = false;
    }

    if (!tracking_active)
    {
        if (rot.park_on_exit)
            RotatorTickPark();
        return;
    }

    RotatorQueueItem *item = RotatorActiveItem();
    if (!item || item->state == RQ_DONE || item->state == RQ_SKIPPED)
    {
        RotatorQueueAdvance(rot_queue.active_idx, now);
        item = RotatorActiveItem();
    }
    if (!item)
    {
        if (rot.park_between_passes)
            RotatorTickPark();
        return;
    }
    if (!item->armed)
        return;

    const struct SatPass *p = RotatorMatchPass(passes_in, pass_count,
                                               item->norad, item->aos);
    if (!p && item->tracked)
        p = RotatorRebindPass(item, passes_in, pass_count, now);
    if (p && p->sat && item->state == RQ_ACTIVE)
    {
        item->aos = p->aos_epoch;
        item->los = p->los_epoch;
    }

    if (item->state == RQ_LOS_HOLD)
    {
        double item_los = (p && p->sat) ? p->los_epoch : item->los;
        // store the LOS the hold enforces so the UI countdown cannot disagree
        item->los = item_los;
        if (now <= RotatorHoldEndEpoch(item_los))
            RotatorCommandSlew(item->los_az, item->los_el, false);
        else
            RotatorFinishAndAdvance(item, RQ_DONE, now);
        return;
    }

    if (!p || !p->sat)
        return;

    if (now > p->los_epoch)
    {
        if (!item->tracked)
        {
            RotatorFinishAndAdvance(item, RQ_SKIPPED, now);
            return;
        }
        if (now <= RotatorHoldEndEpoch(p->los_epoch))
        {
            item->los = p->los_epoch;
            RotatorAzElAtEpoch(p, p->los_epoch, &item->los_az, &item->los_el);
            RotatorPhaseEnter(item, RQ_LOS_HOLD, now);
            RotatorCommandSlew(item->los_az, item->los_el, false);
            return;
        }
        RotatorFinishAndAdvance(item, RQ_DONE, now);
        return;
    }

    int lead_sec = RotatorGetLeadTimeSec();
    double lead_epoch = (lead_sec > 0) ? ((double)lead_sec / 86400.0) : 0.0;
    if (rot.park_between_passes && now < p->aos_epoch - lead_epoch)
    {
        RotatorPhaseEnter(item, RQ_AWAITING, now);
        RotatorTickPark();
        return;
    }

    double t_use = (now < p->aos_epoch) ? p->aos_epoch : now;
    float target_az = 0.0f, target_el = 0.0f;
    RotatorAzElAtEpoch(p, t_use, &target_az, &target_el);

    RotatorPhaseEnter(item, (now < p->aos_epoch) ? RQ_AWAITING_AOS : RQ_ACTIVE, now);
    RotatorTrackTarget(item, target_az, target_el, now, now < p->aos_epoch);
}

bool RotatorIsConnected(void) { return rot.connected; }
float RotatorGetAz(void) { return rot.cur_az; }
float RotatorGetEl(void) { return rot.cur_el; }

bool RotatorHasPosition(void) { return rot.has_position; }

const char *RotatorGetStatusText(void)
{
    return rot.status[0] ? rot.status : "No status";
}

bool RotatorCommLost(void) { return mp_comm_lost; }

bool RotatorQueueIsRunning(void) { return rot_queue_running; }

double RotatorGetLosHoldRemaining(double now_epoch)
{
    if (rot_queue.active_idx < 0 || rot_queue.active_idx >= rot_queue.count)
        return 0.0;
    const RotatorQueueItem *item = &rot_queue.items[rot_queue.active_idx];
    if (item->state != RQ_LOS_HOLD)
        return 0.0;
    int hold_s = RotatorGetLosHoldSec();
    if (hold_s <= 0)
        return 0.0;
    // RotatorTick stores the LOS it holds on, so this window matches exactly
    double end_epoch = item->los + (double)hold_s / 86400.0;
    double remaining = (end_epoch - now_epoch) * 86400.0;
    return remaining > 0.0 ? remaining : 0.0;
}

bool RotatorGetCommandedTarget(float *az_out, float *el_out)
{
    if (!mp_have_last_cmd)
        return false;
    // wire azimuth is unwrapped, the UI wants the compass bearing
    if (az_out) *az_out = RotNorm360((float)mp_last_cmd_az);
    if (el_out) *el_out = (float)mp_last_cmd_el;
    return true;
}

void RotatorGotoAzEl(float az, float el)
{
    // the override latch stops the parked re-send fighting the operator
    mp_manual_override = true;
    // re-seed continuity from live hardware, not the last auto-steer turn count
    RotResetAzBaseline();
    RotatorCommandSlew(az, el, false, true);
}
