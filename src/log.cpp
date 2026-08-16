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

/* -- Public API ------------------------------------------------------------ */

void LogInit(void)
{
    /* nothing dynamic to allocate - buffer is static */
    s_head  = 0;
    s_count = 0;
}

void LogShutdown(void)
{
    /* no-op for now */
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

    /* also write to stderr so it appears in the terminal */
    fprintf(stderr, "[%s] [%s] %s\n",
            entry.timestamp, LevelLabel(level), entry.message);

    /* thread-safe ring-buffer insert */
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        s_buffer[s_head] = entry;
        s_head = (s_head + 1) % LOG_RING_CAPACITY;
        if (s_count < LOG_RING_CAPACITY)
            s_count++;
    }
}

void LogClear(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_head  = 0;
    s_count = 0;
}

int LogGetCount(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_count;
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