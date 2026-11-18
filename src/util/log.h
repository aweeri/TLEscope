#ifndef LOG_H
#define LOG_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Log levels ------------------------------------------------------------ */
/* LOG_LEVEL_ prefix avoids conflicts with raylib's TraceLogLevel enum. */
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

// write ansi to cli and ascii to log
void LogBanner(const char *text, const char *fallback);

/** clear all log entries */
void LogClear(void);

/** lock log buffer for reading; must be paired with LogUnlock(). */
const LogEntry *LogLock(int *out_count);

/** unlock the log buffer after reading */
void LogUnlock(void);

/** ring-buffer head index; oldest entry is at (head - count + capacity) % capacity. */
int LogGetHeadIndex(void);

/** configure file logging; path is copied, file_level 0 disables, max_bytes triggers auto-trim */
void LogConfigureFile(const char *path, int file_level, size_t max_bytes);

/** change the file log level at runtime (0=Off, 1=Debug, 2=Info, 3=Warn, 4=Error) */
void LogSetFileLevel(int file_level);

/** change the file auto-trim threshold at runtime */
void LogSetFileMaxBytes(size_t max_bytes);

/** resolved log file path, or "" when file logging is unconfigured */
const char *LogGetFilePath(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */