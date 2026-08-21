#include "async_fetch.h"
#include "core/astro.h"
#include "data/omm_parser.h"
#include "data/storage.h"
#include "util/log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

/* -- Internal state -------------------------------------------------------- */

static std::deque<AsyncFetchJob> s_job_queue;      // jobs waiting for the worker
static std::deque<AsyncFetchResult> s_results;     // completed jobs (UI thread drains)
static std::mutex s_queue_mutex;
static std::condition_variable s_queue_cv;
static std::thread s_worker;
static std::atomic<bool> s_running{false};
static std::atomic<bool> s_shutdown{false};
static std::atomic<int> s_in_flight{0};            // queued + currently processing

/* -- Worker thread --------------------------------------------------------- */

/** parse a fetched payload into a local Satellite buffer (never the global) */
static int parse_payload(const AsyncFetchJob *job, const char *data, size_t size,
                         Satellite *out, int max)
{
    int count = 0;

    switch (job->type)
    {
        case ASYNC_JOB_CUSTOM_PASTE:
        {
            // paste data is already in memory; parse directly
            if (job->format == FORMAT_TLE)
            {
                const char *ptr = job->paste_data;
                char l0[256], l1[256], l2[256];
                while (*ptr && count < max)
                {
                    while (*ptr == '\r' || *ptr == '\n') ptr++;
                    if (!*ptr) break;
                    if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                    int j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                    l0[j] = '\0'; if (*ptr == '\n') ptr++;
                    j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                    l1[j] = '\0'; if (*ptr == '\n') ptr++;
                    j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                    l2[j] = '\0'; if (*ptr == '\n') ptr++;
                    OrbitalDataMeta meta = {0};
                    snprintf(meta.source_name, sizeof(meta.source_name), "paste:%.31s", job->name);
                    meta.format = FORMAT_TLE;
                    meta.fetch_time = time(NULL);
                    add_satellite_from_tle_to(out, &count, l0, l1, l2, &meta);
                }
            }
            else if (job->format == FORMAT_OMM_JSON)
            {
                ParseOMMJson(job->paste_data, strlen(job->paste_data), out, &count,
                             max, "paste", job->format);
            }
            else if (job->format == FORMAT_OMM_CSV)
            {
                ParseOMMCsv(job->paste_data, strlen(job->paste_data), out, &count,
                            max, "paste", job->format);
            }
            break;
        }

        case ASYNC_JOB_RETLECTOR:
        {
            char url[512];
            snprintf(url, sizeof(url), "https://retlector.eu/%s/csv", job->identifier);
            FetchResult result = FetchFromCustomURL(url);
            if (result.success)
            {
                char source_tag[80];
                snprintf(source_tag, sizeof(source_tag), "retlector:%s", job->identifier);
                ParseOMMCsv(result.data, result.size, out, &count, max, source_tag, FORMAT_OMM_CSV);
                FreeFetchResult(&result);
            }
            return count;
        }

        case ASYNC_JOB_CELESTRAK:
        {
            for (int ci = 0; ci < NUM_CELESTRAK_SOURCES; ci++)
            {
                if (strcmp(CELESTRAK_SOURCES[ci].name, job->identifier) == 0)
                {
                    FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[ci], FORMAT_OMM_CSV);
                    if (result.success)
                    {
                        char source_tag[80];
                        snprintf(source_tag, sizeof(source_tag), "celestrak:%s", job->identifier);
                        ParseOMMCsv(result.data, result.size, out, &count, max, source_tag, FORMAT_OMM_CSV);
                        FreeFetchResult(&result);
                    }
                    break;
                }
            }
            return count;
        }

        case ASYNC_JOB_CUSTOM_URL:
        {
            FetchResult result = FetchFromCustomURL(job->identifier);
            if (result.success)
            {
                if (result.format == FORMAT_TLE)
                {
                    const char *ptr = result.data;
                    char l0[256], l1[256], l2[256];
                    while (*ptr && count < max)
                    {
                        while (*ptr == '\r' || *ptr == '\n') ptr++;
                        if (!*ptr) break;
                        if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                        int j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                        l0[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                        l1[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                        l2[j] = '\0'; if (*ptr == '\n') ptr++;
                        OrbitalDataMeta meta = {0};
                        snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s", job->name);
                        meta.format = result.format;
                        meta.fetch_time = time(NULL);
                        add_satellite_from_tle_to(out, &count, l0, l1, l2, &meta);
                    }
                }
                else if (result.format == FORMAT_OMM_JSON)
                {
                    ParseOMMJson(result.data, result.size, out, &count, max, job->name, result.format);
                }
                else if (result.format == FORMAT_OMM_CSV)
                {
                    ParseOMMCsv(result.data, result.size, out, &count, max, job->name, result.format);
                }
                FreeFetchResult(&result);
            }
            return count;
        }
    }

    return count;
}

static void worker_main(void)
{
    while (true)
    {
        AsyncFetchJob job;
        {
            std::unique_lock<std::mutex> lock(s_queue_mutex);
            s_queue_cv.wait(lock, [] { return s_shutdown.load() || !s_job_queue.empty(); });
            if (s_shutdown.load() && s_job_queue.empty())
                break;
            job = s_job_queue.front();
            s_job_queue.pop_front();
        }

        // fetch + parse into a heap-allocated buffer (never the global satellites[])
        AsyncFetchResult res;
        memset(&res, 0, sizeof(res));
        res.job = job;
        res.parsed = (Satellite*)malloc(MAX_SATELLITES * sizeof(Satellite));
        if (res.parsed)
        {
            res.parsed_count = parse_payload(&job, NULL, 0, res.parsed, MAX_SATELLITES);
            res.success = (res.parsed_count > 0);
        }

        {
            std::lock_guard<std::mutex> lock(s_queue_mutex);
            s_results.push_back(res);
            s_in_flight.fetch_sub(1);
        }
    }
}

/* -- Public API ------------------------------------------------------------ */

void AsyncFetchInit(void)
{
    if (s_running.load()) return;
    s_shutdown = false;
    s_running = true;
    s_worker = std::thread(worker_main);
}

void AsyncFetchShutdown(void)
{
    if (!s_running.load()) return;
    {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_shutdown = true;
    }
    s_queue_cv.notify_all();
    if (s_worker.joinable())
        s_worker.join();
    s_running = false;
}

void AsyncFetchSubmit(const AsyncFetchJob *job)
{
    if (!job) return;
    if (!s_running.load())
        AsyncFetchInit();

    {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_job_queue.push_back(*job);
        s_in_flight.fetch_add(1);
    }
    s_queue_cv.notify_one();
}

bool AsyncFetchHasResults(void)
{
    std::lock_guard<std::mutex> lock(s_queue_mutex);
    return !s_results.empty();
}

bool AsyncFetchPopResult(AsyncFetchResult *out)
{
    if (!out) return false;
    std::lock_guard<std::mutex> lock(s_queue_mutex);
    if (s_results.empty()) return false;
    *out = s_results.front();
    s_results.pop_front();
    return true;
}

int AsyncFetchPendingCount(void)
{
    return s_in_flight.load();
}

bool AsyncFetchBusy(void)
{
    return s_in_flight.load() > 0;
}

/* -- UI-thread result application ------------------------------------------ */

void AsyncFetchFreeResult(AsyncFetchResult *res)
{
    if (res && res->parsed)
    {
        free(res->parsed);
        res->parsed = NULL;
        res->parsed_count = 0;
        res->success = false;
    }
}

void AsyncFetchApplyResults(void)
{
    bool any_applied = false;

    // drain the queue and copy parsed satellites into the global array
    AsyncFetchResult res;
    while (AsyncFetchPopResult(&res))
    {
        any_applied = true;

        if (!res.success || res.parsed_count <= 0)
        {
            LOG_WARN("Async fetch produced no satellites for source: %s", res.job.name);
            AsyncFetchFreeResult(&res);
            continue;
        }

        // copy into the global array (UI thread only - safe with the render loop)
        int room = MAX_SATELLITES - sat_count;
        int to_copy = (res.parsed_count < room) ? res.parsed_count : room;
        if (to_copy > 0)
        {
            memcpy(&satellites[sat_count], res.parsed, to_copy * sizeof(Satellite));
            sat_count += to_copy;
            LOG_INFO("Applied %d satellites from %s (total: %d)",
                     to_copy, res.job.name, sat_count);
        }
        else
        {
            LOG_WARN("MAX_SATELLITES reached - dropped %d satellites from %s",
                     res.parsed_count, res.job.name);
        }

        AsyncFetchFreeResult(&res);
    }

    // when the last job finishes, persist the combined dataset
    if (any_applied && !AsyncFetchBusy())
    {
        SaveOrbitalData("data.json", satellites, sat_count);
        LOG_INFO("Async pull complete: %d satellites total", sat_count);
    }
}