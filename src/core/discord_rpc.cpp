// Discord IPC wire format: 4-byte LE opcode, 4-byte LE payload length, then a UTF-8 JSON payload.

#include "discord_rpc.h"

#include <raymath.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "util/log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#define DISCORD_CLIENT_ID "1570402312319406090"

#define OP_HANDSHAKE 0
#define OP_FRAME     1
#define OP_CLOSE     2
#define OP_PING      3
#define OP_PONG      4

#define RECONNECT_INTERVAL 5.0    // seconds between connection attempts
#define IDLE_ROTATE_INTERVAL 20.0 // seconds between idle message rotations

static const char *const IDLE_MESSAGES[] = {
    "Staring at the sky",
    "Waiting for a satellite to be selected",
    "Counting stars... 1, 2, 3...",
    "Orbiting the idea of picking a satellite",
    "About to collide with Starlink",
    "Lost in space",
    "Pondering orbital mechanics",
    "Beep boop, no satellite selected",
    "Admiring the curvature of the Earth",
    "Crashing SatDump...",
};
static const int IDLE_MESSAGE_COUNT = (int)(sizeof(IDLE_MESSAGES) / sizeof(IDLE_MESSAGES[0]));

static bool g_enabled = false;
static bool g_connected = false;
static bool g_ready = false;

static double g_app_start_unix = 0.0;
static double g_last_connect_attempt = 0.0;
static double g_last_send = 0.0;
static double g_last_idle_rotate = 0.0;

static int g_idle_index = 0;
static bool g_force_send = true;

static const Satellite *g_cached_sat = NULL;
static int g_cached_alt_bucket = -1;
static int g_cached_sat_count = -1;

static std::vector<char> g_read_buf;

#ifdef _WIN32
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
#else
static int g_sock = -1;
#endif

static double NowSeconds(void)
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static int CurrentPid(void)
{
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

static std::string MakeNonce(void)
{
    static unsigned long counter = 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "%08lx%08lx%08lx%08lx",
             (unsigned long)time(NULL), (unsigned long)rand(),
             (unsigned long)rand(), counter++);
    return std::string(buf);
}

#ifdef _WIN32

static bool TransportConnect(int index)
{
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\pipe\\discord-ipc-%d", index);
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    g_pipe = h;
    return true;
}

static void TransportClose(void)
{
    if (g_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

static bool TransportWrite(const void *data, size_t len)
{
    if (g_pipe == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    if (!WriteFile(g_pipe, data, (DWORD)len, &written, NULL))
        return false;
    return written == (DWORD)len;
}

static int TransportRead(void *buf, size_t len)
{
    if (g_pipe == INVALID_HANDLE_VALUE)
        return -1;
    DWORD avail = 0;
    if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL))
        return -1;
    if (avail == 0)
        return 0;
    DWORD to_read = (avail < (DWORD)len) ? avail : (DWORD)len;
    DWORD read = 0;
    if (!ReadFile(g_pipe, buf, to_read, &read, NULL))
        return -1;
    return (int)read;
}

#else

static bool TransportConnect(int index)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char path[256];
    if (runtime && runtime[0])
        snprintf(path, sizeof(path), "%s/discord-ipc-%d", runtime, index);
    else
        snprintf(path, sizeof(path), "/tmp/discord-ipc-%d", index);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    g_sock = fd;
    return true;
}

static void TransportClose(void)
{
    if (g_sock >= 0)
    {
        close(g_sock);
        g_sock = -1;
    }
}

static bool TransportWrite(const void *data, size_t len)
{
    if (g_sock < 0)
        return false;
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len)
    {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(g_sock, p + sent, len - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(g_sock, p + sent, len - sent, 0);
#endif
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

static int TransportRead(void *buf, size_t len)
{
    if (g_sock < 0)
        return -1;
    ssize_t n = recv(g_sock, buf, len, 0);
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }
    return (int)n;
}

#endif

static bool WriteFrame(uint32_t opcode, const std::string &payload)
{
    uint32_t header[2];
    header[0] = opcode;
    header[1] = (uint32_t)payload.size();
    if (!TransportWrite(header, sizeof(header)))
        return false;
    if (!payload.empty() && !TransportWrite(payload.data(), payload.size()))
        return false;
    return true;
}

static void HandleFrame(uint32_t opcode, const std::string &payload)
{
    if (opcode == OP_PING)
    {
        WriteFrame(OP_PONG, payload);
    }
    else if (opcode == OP_FRAME)
    {
        try
        {
            nlohmann::json j = nlohmann::json::parse(payload);
            if (j.contains("evt") && j["evt"] == "READY")
            {
                g_ready = true;
                LOG_INFO("Discord RPC: READY");
            }
        }
        catch (...)
        {
        }
    }
    else if (opcode == OP_CLOSE)
    {
        TransportClose();
        g_connected = false;
    }
}

static void Pump(void)
{
    char tmp[4096];
    for (;;)
    {
        int n = TransportRead(tmp, sizeof(tmp));
        if (n <= 0)
        {
            if (n < 0)
            {
                TransportClose();
                g_connected = false;
            }
            break;
        }
        g_read_buf.insert(g_read_buf.end(), tmp, tmp + n);
    }

    size_t off = 0;
    while (g_read_buf.size() - off >= 8)
    {
        uint32_t opcode = 0;
        uint32_t length = 0;
        memcpy(&opcode, &g_read_buf[off], 4);
        memcpy(&length, &g_read_buf[off + 4], 4);
        if (g_read_buf.size() - off - 8 < length)
            break;
        std::string payload(&g_read_buf[off + 8], length);
        HandleFrame(opcode, payload);
        off += 8 + length;
    }
    if (off > 0)
        g_read_buf.erase(g_read_buf.begin(), g_read_buf.begin() + off);
}

static void Connect(void)
{
    for (int i = 0; i < 10; i++)
    {
        if (!TransportConnect(i))
            continue;

        g_connected = true;
        g_ready = false;
        g_read_buf.clear();

        nlohmann::json hs;
        hs["v"] = 1;
        hs["client_id"] = DISCORD_CLIENT_ID;
        if (!WriteFrame(OP_HANDSHAKE, hs.dump()))
        {
            TransportClose();
            g_connected = false;
            continue;
        }

        LOG_INFO("Discord RPC: connected on discord-ipc-%d", i);
        return;
    }
}

static const char *OrbitClass(const Satellite *sat)
{
    double mm = sat->mean_motion * 86400.0 / (2.0 * PI); 
    double ecc = sat->eccentricity;
    if (ecc > 0.25)
        return "HEO";
    if (mm >= 0.9 && mm <= 1.1)
        return "GEO";
    if (mm > 11.25)
        return "LEO";
    return "MEO";
}

static void SendActivity(const Satellite *selected, int sat_count)
{
    nlohmann::json activity;

    if (selected)
    {
        float alt = Vector3Length(selected->current_pos) - EARTH_RADIUS_KM;
        const char *cls = OrbitClass(selected);

        char state[160];
        snprintf(state, sizeof(state), "NORAD %s \xE2\x80\xA2 %.0f km \xE2\x80\xA2 %s",
                 selected->norad_id, alt, cls);

        activity["details"] = std::string("Tracking ") + selected->name;
        activity["state"] = state;
        activity["assets"]["large_image"] = "tlescope_logo";
        activity["assets"]["large_text"] = "TLEscope";
        activity["assets"]["small_image"] = "satellite";
        activity["assets"]["small_text"] = cls;
    }
    else
    {
        char state[128];
        snprintf(state, sizeof(state), "TLEscope \xE2\x80\xA2 %d satellites loaded", sat_count);

        activity["details"] = IDLE_MESSAGES[g_idle_index];
        activity["state"] = state;
        activity["assets"]["large_image"] = "tlescope_logo";
        activity["assets"]["large_text"] = "TLEscope";
        activity["assets"]["small_image"] = "idle";
        activity["assets"]["small_text"] = "Idle";
    }

    activity["buttons"] = nlohmann::json::array({{{"label", "tlescope.eu"}, {"url", "https://tlescope.eu"}}});
    activity["timestamps"]["start"] = (long long)g_app_start_unix;

    nlohmann::json frame;
    frame["cmd"] = "SET_ACTIVITY";
    frame["args"]["pid"] = CurrentPid();
    frame["args"]["activity"] = activity;
    frame["nonce"] = MakeNonce();

    LOG_INFO("Discord RPC: sending activity %s", activity.dump().c_str()); // diagnostic: confirm buttons present
    WriteFrame(OP_FRAME, frame.dump());
}

void DiscordRpcInit(bool enabled)
{
    g_enabled = enabled;
    g_connected = false;
    g_ready = false;
    g_app_start_unix = (double)time(NULL);
    g_last_connect_attempt = 0.0;
    g_last_send = 0.0;
    g_last_idle_rotate = NowSeconds();
    g_idle_index = 0;
    g_force_send = true;
    g_cached_sat = NULL;
    g_cached_alt_bucket = -1;
    g_cached_sat_count = -1;
    g_read_buf.clear();

    if (g_enabled)
    {
        Connect();
        g_last_connect_attempt = NowSeconds();
    }
}

void DiscordRpcShutdown(void)
{
    if (g_connected)
        WriteFrame(OP_CLOSE, "{}");
    TransportClose();
    g_connected = false;
    g_ready = false;
    g_read_buf.clear();
}

void DiscordRpcSetEnabled(bool enabled)
{
    if (enabled == g_enabled)
        return;
    g_enabled = enabled;
    if (!enabled)
    {
        DiscordRpcShutdown();
    }
    else
    {
        g_last_connect_attempt = 0.0;
        g_force_send = true;
    }
}

bool DiscordRpcIsConnected(void)
{
    return g_connected;
}

void DiscordRpcUpdate(const Satellite *selected, int sat_count, double epoch)
{
    (void)epoch;

    if (!g_enabled)
        return;

    double now = NowSeconds();

    if (!g_connected)
    {
        if (now - g_last_connect_attempt >= RECONNECT_INTERVAL)
        {
            g_last_connect_attempt = now;
            Connect();
        }
        return;
    }

    Pump();
    if (!g_connected)
        return;

    if (now - g_last_idle_rotate >= IDLE_ROTATE_INTERVAL)
    {
        g_last_idle_rotate = now;
        g_idle_index = (g_idle_index + 1) % IDLE_MESSAGE_COUNT;
        g_force_send = true;
    }

    int alt_bucket = 0;
    if (selected)
    {
        float alt = Vector3Length(selected->current_pos) - EARTH_RADIUS_KM;
        alt_bucket = (int)(alt / 10.0f);
    }

    bool changed = (selected != g_cached_sat) || (alt_bucket != g_cached_alt_bucket) ||
                   (sat_count != g_cached_sat_count);

    if (changed || g_force_send)
    {
        SendActivity(selected, sat_count);
        g_cached_sat = selected;
        g_cached_alt_bucket = alt_bucket;
        g_cached_sat_count = sat_count;
        g_last_send = now;
        g_force_send = false;
    }
}
