#ifndef LOG_H
#define LOG_H

/**
 * @file log.h
 * @brief Ring-buffer logging with printf-style macros
 */

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Log levels ------------------------------------------------------------ */
/* NOTE: values are prefixed with LOG_LEVEL_ to avoid conflicts with
 * raylib's TraceLogLevel enum (LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR). */
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/* -- Log entry ------------------------------------------------------------- */
typedef struct {
    char timestamp[32];   /* formatted timestamp string */
    LogLevel level;
    char message[512];    /* log message text */
} LogEntry;

/* -- Configuration --------------------------------------------------------- */
#define LOG_RING_CAPACITY  512   /* number of entries in the ring buffer */

/* -- Public API ------------------------------------------------------------ */

/** initialize the log system (call once at startup) */
void LogInit(void);

/** shut down the log system and free resources */
void LogShutdown(void);

/** append a log entry with the given level and printf-style format */
void LogMessage(LogLevel level, const char *format, ...);

/* convenience macros */
#define LOG_DEBUG(...)  LogMessage(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)   LogMessage(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_WARN(...)   LogMessage(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_ERROR(...)  LogMessage(LOG_LEVEL_ERROR, __VA_ARGS__)

/** clear all log entries */
void LogClear(void);

/** get the total number of entries currently in the buffer */
int LogGetCount(void);

/** lock the log buffer for reading; returns pointer to entries array and count.
 *  Must be paired with LogUnlock(). The entries are in ring order; use
 *  LogGetCount() to know how many are valid. */
const LogEntry *LogLock(int *out_count);

/** unlock the log buffer after reading */
void LogUnlock(void);

/** get the ring-buffer head index (next write position).
 *  Used together with LogLock() to read entries in chronological order:
 *  the oldest entry is at (head - count + capacity) % capacity. */
int LogGetHeadIndex(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */