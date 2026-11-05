#ifndef ASYNC_FETCH_H
#define ASYNC_FETCH_H

/**
 * @file async_fetch.h
 * @brief Asynchronous data-source fetching
 *
 * Moves the blocking network I/O (and parsing) of the "Pull All Selected
 * Sources" action off the UI thread. Jobs are enqueued, processed by a
 * background worker thread, and the parsed results are handed back to the
 * UI thread via a completion queue that is drained once per frame.
 *
 * The worker thread NEVER touches the global `satellites[]` array. Each job
 * parses into its own local buffer; the UI thread copies those results into
 * the global array when it drains the completion queue. This avoids data
 * races with the render loop.
 */

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
