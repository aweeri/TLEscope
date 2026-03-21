#define _GNU_SOURCE
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
typedef struct tagMSG *LPMSG;
#endif
#include "rotator.h"
#include "astro.h"
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
#include <sys/time.h>
#include <unistd.h>
#endif

typedef struct
{
    char host[64];
    char port[16];
    char get_fmt[64];
    char set_fmt[64];
    char custom_cmd[128];
    char park_az[16];
    char park_el[16];
    char lead_time[16];

    bool auto_steer;
    int steer_mode;

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
    .host = "127.0.0.1",
    .port = "4533",
    .get_fmt = "p",
    .set_fmt = "P %.1f %.1f",
    .custom_cmd = "",
    .park_az = "180.0",
    .park_el = "0.0",
    .lead_time = "30",
    .auto_steer = true,
    .steer_mode = ROTATOR_STEER_POLAR,
    .connected = false,
    .sock = -1,
    .cur_az = 0.0f,
    .cur_el = 0.0f,
    .has_position = false,
    .last_poll_time = 0.0,
    .last_send_time = 0.0,
    .status = "Disconnected"};

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
        snprintf(rot.status, sizeof(rot.status), "Connection failed");
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    DWORD timeout_ms = 150;
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv = {.tv_sec = 0, .tv_usec = 150000};
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    rot.sock = sfd;
    rot.connected = true;
    snprintf(rot.status, sizeof(rot.status), "Connected to %s:%s", host, port);
    return true;
}

static bool SendRaw(const char *cmd, char *response, size_t response_len)
{
    if (!rot.connected || rot.sock < 0 || !cmd || cmd[0] == '\0')
        return false;

    char out[256];
    size_t cmd_len = strlen(cmd);
    if (cmd_len >= sizeof(out) - 2)
        cmd_len = sizeof(out) - 2;
    memcpy(out, cmd, cmd_len);
    if (cmd_len == 0 || out[cmd_len - 1] != '\n')
        out[cmd_len++] = '\n';
    out[cmd_len] = '\0';

#if defined(_WIN32) || defined(_WIN64)
    int sent = send((SOCKET)rot.sock, out, (int)cmd_len, 0);
#else
    int sent = (int)send(rot.sock, out, cmd_len, 0);
#endif
    if (sent <= 0)
    {
        snprintf(rot.status, sizeof(rot.status), "Send failed");
        Disconnect();
        return false;
    }

    {
        char discard[256] = {0};
        char *resp_buf = response;
        size_t resp_len = response_len;
        if (!resp_buf || resp_len == 0)
        {
            resp_buf = discard;
            resp_len = sizeof(discard);
        }

#if defined(_WIN32) || defined(_WIN64)
        int n = recv((SOCKET)rot.sock, resp_buf, (int)resp_len - 1, 0);
#else
        int n = (int)recv(rot.sock, resp_buf, resp_len - 1, 0);
#endif
        if (n <= 0)
        {
            if (response && response_len > 0)
                response[0] = '\0';
            snprintf(rot.status, sizeof(rot.status), "Read failed");
            Disconnect();
            return false;
        }
        resp_buf[n] = '\0';
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

static void PollPosition(void)
{
    if (!rot.connected)
        return;
    if (GetTime() - rot.last_poll_time < 0.5)
        return;
    rot.last_poll_time = GetTime();

    char response[256] = {0};
    if (!SendRaw(rot.get_fmt, response, sizeof(response)))
        return;

    float az = 0.0f, el = 0.0f;
    if (ParseFirstTwoFloats(response, &az, &el))
    {
        rot.cur_az = az;
        rot.cur_el = el;
        rot.has_position = true;
        snprintf(rot.status, sizeof(rot.status), "OK");
    }
    else
    {
        snprintf(rot.status, sizeof(rot.status), "Parse failed");
    }
}

static bool SetPosition(float az, float el)
{
    if (!rot.connected)
        return false;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), rot.set_fmt, az, el);
    bool ok = SendRaw(cmd, NULL, 0);
    if (ok)
        rot.last_send_time = GetTime();
    return ok;
}

void RotatorShutdown(void) { Disconnect(); }

char *RotatorGetHostBuffer(void) { return rot.host; }
int RotatorGetHostBufferSize(void) { return (int)sizeof(rot.host); }
char *RotatorGetPortBuffer(void) { return rot.port; }
int RotatorGetPortBufferSize(void) { return (int)sizeof(rot.port); }
char *RotatorGetGetFmtBuffer(void) { return rot.get_fmt; }
int RotatorGetGetFmtBufferSize(void) { return (int)sizeof(rot.get_fmt); }
char *RotatorGetSetFmtBuffer(void) { return rot.set_fmt; }
int RotatorGetSetFmtBufferSize(void) { return (int)sizeof(rot.set_fmt); }
char *RotatorGetCustomCmdBuffer(void) { return rot.custom_cmd; }
int RotatorGetCustomCmdBufferSize(void) { return (int)sizeof(rot.custom_cmd); }
char *RotatorGetParkAzBuffer(void) { return rot.park_az; }
int RotatorGetParkAzBufferSize(void) { return (int)sizeof(rot.park_az); }
char *RotatorGetParkElBuffer(void) { return rot.park_el; }
int RotatorGetParkElBufferSize(void) { return (int)sizeof(rot.park_el); }
const char *RotatorGetStatus(void) { return rot.status; }
bool RotatorGetAutoSteer(void) { return rot.auto_steer; }
void RotatorSetAutoSteer(bool enabled) { rot.auto_steer = enabled; }
int RotatorGetSteerMode(void) { return rot.steer_mode; }
void RotatorSetSteerMode(int mode) { rot.steer_mode = mode; }
int RotatorGetLeadTimeSec(void) { return (int)atol(rot.lead_time); }
void RotatorSetLeadTimeSec(int sec) { snprintf(rot.lead_time, sizeof(rot.lead_time), "%d", sec); }
char *RotatorGetLeadTimeBuffer(void) { return rot.lead_time; }
int RotatorGetLeadTimeBufferSize(void) { return (int)sizeof(rot.lead_time); }
void RotatorConnect(void) { ConnectTcp(rot.host, rot.port); }
void RotatorDisconnect(void)
{
    Disconnect();
    snprintf(rot.status, sizeof(rot.status), "Disconnected");
}
void RotatorPollNow(void) { PollPosition(); }
void RotatorSendCustomNow(void)
{
    if (rot.custom_cmd[0] != '\0')
        SendRaw(rot.custom_cmd, NULL, 0);
}
void RotatorSetParkNow(float az, float el) { SetPosition(az, el); }

void RotatorUpdateControl(UIContext *ctx, bool show_scope_dialog, bool show_polar_dialog, bool polar_lunar_mode, int selected_pass_idx)
{
    if (rot.connected)
        PollPosition();

    if (rot.connected && rot.auto_steer && (GetTime() - rot.last_send_time) > 0.25)
    {
        float target_az = 0.0f, target_el = 0.0f;
        bool has_target = false;

        if (rot.steer_mode == ROTATOR_STEER_SCOPE && show_scope_dialog)
        {
            target_az = *ctx->scope_az;
            target_el = *ctx->scope_el;
            has_target = true;
        }
        else if (rot.steer_mode == ROTATOR_STEER_POLAR && show_polar_dialog && !polar_lunar_mode && selected_pass_idx >= 0 && selected_pass_idx < num_passes)
        {
            SatPass *p = &passes[selected_pass_idx];
            int lead_sec = RotatorGetLeadTimeSec();
            double lead_epoch = (lead_sec > 0) ? (lead_sec / 86400.0) : 0.0;
            double steer_start = p->aos_epoch - lead_epoch;
            if (*ctx->current_epoch >= steer_start && *ctx->current_epoch <= p->los_epoch && p->sat != NULL)
            {
                double t_use = (*ctx->current_epoch < p->aos_epoch) ? p->aos_epoch : *ctx->current_epoch;
                double gmst_use = (t_use == p->aos_epoch) ? epoch_to_gmst(p->aos_epoch) : ctx->gmst_deg;
                Vector3 sat_pos = calculate_position(p->sat, get_unix_from_epoch(t_use));
                double az = 0.0, el = 0.0;
                get_az_el(sat_pos, gmst_use, home_location.lat, home_location.lon, home_location.alt, &az, &el);
                target_az = (float)az;
                target_el = (float)el;
                has_target = true;
            }
        }

        if (has_target)
        {
            while (target_az < 0.0f)
                target_az += 360.0f;
            while (target_az >= 360.0f)
                target_az -= 360.0f;
            if (target_el > 90.0f)
                target_el = 90.0f;
            if (target_el < -90.0f)
                target_el = -90.0f;
            SetPosition(target_az, target_el);
        }
    }
}

bool RotatorIsConnected(void) { return rot.connected; }
bool RotatorHasPosition(void) { return rot.has_position; }
float RotatorGetAz(void) { return rot.cur_az; }
float RotatorGetEl(void) { return rot.cur_el; }
