#include "log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>

/* -- Ring buffer state ----------------------------------------------------- */

static LogEntry s_buffer[LOG_RING_CAPACITY];
static int     s_head = 0;          /* next write position */
static int     s_count = 0;         /* number of valid entries */
static std::mutex s_mutex;

/* -- File logging state ---------------------------------------------------- */

static FILE       *s_file = NULL;
static char        s_file_path[1024];
static size_t      s_file_bytes = 0;
static size_t      s_file_max = 0;
static int         s_file_min_level = 0;
static std::mutex  s_file_mutex;

/* -- Level label strings --------------------------------------------------- */

static const char *LevelLabel(LogLevel level)
{
    switch (level)
    {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "????";
    }
}

/* -- ANSI escape stripping ------------------------------------------------- */

static void StripAnsi(const char *in, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;

    size_t o = 0;
    for (size_t i = 0; in && in[i] != '\0' && o + 1 < out_size; )
    {
        if (in[i] == '\x1b' && in[i + 1] == '[')
        {
            i += 2;
            while (in[i] != '\0' && in[i] != 'm')
                i++;
            if (in[i] == 'm')
                i++;
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
}

/* -- File logging ---------------------------------------------------------- */

/* drop the oldest lines until the file fits within s_file_max (keep 3/4) */
static void TrimFile(void)
{
    if (!s_file || s_file_bytes <= s_file_max)
        return;

    size_t keep = s_file_max * 3 / 4;

    fclose(s_file);
    s_file = NULL;

    FILE *in = fopen(s_file_path, "rb");
    if (!in)
        return;

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    if (size < 0) size = 0;
    long start = size - (long)keep;
    if (start < 0) start = 0;
    fseek(in, start, SEEK_SET);

    /* drop the partial first line */
    if (start > 0)
    {
        int c;
        while ((c = fgetc(in)) != EOF && c != '\n')
            ;
    }

    char tmp_path[1040];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", s_file_path);

    FILE *out = fopen(tmp_path, "wb");
    if (!out)
    {
        fclose(in);
        return;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in);
    fclose(out);

    remove(s_file_path);
    rename(tmp_path, s_file_path);

    s_file = fopen(s_file_path, "ab");
    if (s_file)
    {
        fseek(s_file, 0, SEEK_END);
        long new_size = ftell(s_file);
        s_file_bytes = (new_size > 0) ? (size_t)new_size : 0;
    }
    else
    {
        s_file_bytes = 0;
    }
}

/* -- Public API ------------------------------------------------------------ */

void LogInit(void)
{
    /* nothing dynamic to allocate - buffer is static */
    s_head  = 0;
    s_count = 0;
}

void LogShutdown(void)
{
    std::lock_guard<std::mutex> lock(s_file_mutex);
    if (s_file)
    {
        fclose(s_file);
        s_file = NULL;
    }
}

void LogMessage(LogLevel level, const char *format, ...)
{
    if (!format || !format[0]) return;

    LogEntry entry;
    entry.level = level;

    /* build timestamp */
    time_t raw = time(nullptr);
    struct tm *local = localtime(&raw);
    if (local)
    {
        strftime(entry.timestamp, sizeof(entry.timestamp),
                 "%H:%M:%S", local);
    }
    else
    {
        snprintf(entry.timestamp, sizeof(entry.timestamp), "??:??:??");
    }

    /* format message */
    va_list args;
    va_start(args, format);
    vsnprintf(entry.message, sizeof(entry.message), format, args);
    va_end(args);

    /* also write to stdout so it appears in the terminal */
    fprintf(stdout, "[%s] [%s] %s\n",
            entry.timestamp, LevelLabel(level), entry.message);
    fflush(stdout);

    /* append to the log file (own mutex; never hold s_mutex during disk I/O) */
    {
        std::lock_guard<std::mutex> lock(s_file_mutex);
        if (s_file && (int)level >= s_file_min_level)
        {
            char full_ts[32];
            if (local)
                strftime(full_ts, sizeof(full_ts), "%Y-%m-%d %H:%M:%S", local);
            else
                snprintf(full_ts, sizeof(full_ts), "0000-00-00 00:00:00");

            int written = fprintf(s_file, "%s [%s] %s\n",
                                  full_ts, LevelLabel(level), entry.message);
            if (written > 0)
                s_file_bytes += (size_t)written;
            fflush(s_file);
            if (s_file_bytes > s_file_max)
                TrimFile();
        }
    }

    /* thread-safe ring-buffer insert */
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        s_buffer[s_head] = entry;
        s_head = (s_head + 1) % LOG_RING_CAPACITY;
        if (s_count < LOG_RING_CAPACITY)
            s_count++;
    }
}

void LogBanner(const char *text, const char *fallback)
{
    if (!text) return;

    /* terminal view: raw ANSI art verbatim, no [timestamp] [LEVEL] prefix */
    fputs(text, stdout);
    fputc('\n', stdout);
    fflush(stdout);

    /* in-app view: the theme font lacks Block Elements, so use the ASCII fallback */
    const char *line = fallback ? fallback : text;
    while (*line)
    {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);

        char raw[2048];
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        memcpy(raw, line, len);
        raw[len] = '\0';

        LogEntry entry;
        entry.timestamp[0] = '\0';
        entry.level = LOG_LEVEL_INFO;
        StripAnsi(raw, entry.message, sizeof(entry.message));

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_buffer[s_head] = entry;
            s_head = (s_head + 1) % LOG_RING_CAPACITY;
            if (s_count < LOG_RING_CAPACITY)
                s_count++;
        }

        if (!nl) break;
        line = nl + 1;
    }
}

void LogClear(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_head  = 0;
    s_count = 0;
}

const LogEntry *LogLock(int *out_count)
{
    s_mutex.lock();
    if (out_count) *out_count = s_count;
    return s_buffer;
}

void LogUnlock(void)
{
    s_mutex.unlock();
}

int LogGetHeadIndex(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_head;
}

void LogConfigureFile(const char *path, int file_level, size_t max_bytes)
{
    std::lock_guard<std::mutex> lock(s_file_mutex);

    if (s_file)
    {
        fclose(s_file);
        s_file = NULL;
    }

    s_file_bytes = 0;
    s_file_max = max_bytes;
    s_file_min_level = (file_level >= 1) ? (file_level - 1) : 4;

    if (path && path[0])
        snprintf(s_file_path, sizeof(s_file_path), "%s", path);
    else
        s_file_path[0] = '\0';

    if (file_level <= 0 || s_file_path[0] == '\0')
        return;

    s_file = fopen(s_file_path, "ab");
    if (!s_file)
    {
        s_file_path[0] = '\0';
        return;
    }

    fseek(s_file, 0, SEEK_END);
    long size = ftell(s_file);
    s_file_bytes = (size > 0) ? (size_t)size : 0;

    TrimFile();
}

void LogSetFileLevel(int file_level)
{
    std::lock_guard<std::mutex> lock(s_file_mutex);

    if (file_level <= 0)
    {
        s_file_min_level = 4;
        if (s_file)
        {
            fclose(s_file);
            s_file = NULL;
        }
        return;
    }

    s_file_min_level = file_level - 1;

    /* reopen if file logging was previously disabled */
    if (!s_file && s_file_path[0] != '\0')
    {
        s_file = fopen(s_file_path, "ab");
        if (s_file)
        {
            fseek(s_file, 0, SEEK_END);
            long size = ftell(s_file);
            s_file_bytes = (size > 0) ? (size_t)size : 0;
            TrimFile();
        }
    }
}

void LogSetFileMaxBytes(size_t max_bytes)
{
    std::lock_guard<std::mutex> lock(s_file_mutex);
    s_file_max = max_bytes;
    if (s_file && s_file_bytes > s_file_max)
        TrimFile();
}

const char *LogGetFilePath(void)
{
    return s_file_path[0] ? s_file_path : "";
}