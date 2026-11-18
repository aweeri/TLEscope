#ifndef ASYNC_FETCH_H
#define ASYNC_FETCH_H

/* Worker thread never touches the global satellites[] array; results return via a completion queue. */

#include "core/types.h"
#include "data/provider.h"
#include <stdbool.h>

/* -- Job definition -------------------------------------------------------- */

typedef enum {
    ASYNC_JOB_RETLECTOR,
    ASYNC_JOB_CELESTRAK,
    ASYNC_JOB_CUSTOM_URL,
    ASYNC_JOB_CUSTOM_FILE,
    ASYNC_JOB_CUSTOM_PASTE
} AsyncJobType;

typedef struct {
    AsyncJobType type;
    char name[64];          // display name (for source tagging)
    char identifier[64];    // group name / URL
    char paste_data[4096];  // raw pasted data (CUSTOM_PASTE only)
    OrbitalDataFormat format; // requested/detected format
} AsyncFetchJob;

// completed job: parsed satellites live in a heap buffer owned by the UI thread; free with AsyncFetchFreeResult()
typedef struct {
    AsyncFetchJob job;
    bool success;
    int parsed_count;       // number of satellites parsed
    Satellite *parsed;      // heap-allocated buffer (NOT the global array)
} AsyncFetchResult;

/** free the heap-allocated parsed buffer of a popped result */
void AsyncFetchFreeResult(AsyncFetchResult *res);

// -- Lifecycle -------------------------------------------------------------

/** initialize the async fetch subsystem (idempotent) */
void AsyncFetchInit(void);

/** shut down the worker thread and release resources (call on exit) */
void AsyncFetchShutdown(void);

// -- Job submission --------------------------------------------------------

/** enqueue a job to be fetched/parsed on the worker thread */
void AsyncFetchSubmit(const AsyncFetchJob *job);

/* -- Completion queue (drained on the UI thread) --------------------------- */

/** returns true if at least one completed job is waiting to be applied */
bool AsyncFetchHasResults(void);

// pop the next completed job; returns false if the queue is empty
bool AsyncFetchPopResult(AsyncFetchResult *out);

/** returns the number of jobs still queued or in flight (for progress UI) */
int AsyncFetchPendingCount(void);

// completed jobs that produced no satellites since the last call (resets the counter); UI thread only
int AsyncFetchTakeFailureCount(void);

/** returns true while the worker thread is still processing jobs */
bool AsyncFetchBusy(void);

// drain the completion queue into the global array (once per frame, UI thread); saves to disk when the last job finishes
void AsyncFetchApplyResults(void);

#endif // ASYNC_FETCH_H
