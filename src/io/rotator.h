#ifndef ROTATOR_H
#define ROTATOR_H

#include "ui/ui.h"
#include "core/astro.h"
#include <nlohmann/json.hpp>

#define ROTATOR_STEER_POLAR 0

#define MAX_ROTATOR_QUEUE 64

// values are frozen for persisted queues, 4 was RQ_FAILED
typedef enum
{
    RQ_PENDING,
    RQ_ACTIVE,
    RQ_DONE,
    RQ_SKIPPED,
    // 4 reserved, never reuse
    RQ_AWAITING = 5,
    RQ_AWAITING_AOS,
    RQ_LOS_HOLD = 7
} RotatorQueueState;

typedef struct
{
    uint32_t norad;
    double aos;
    double los;
    double max_el_epoch;
    float max_el;
    RotatorQueueState state;
    bool armed;
    bool tracked;
    bool arrived;
    float los_az;
    float los_el;
    // sim epoch, re-armed on every phase change
    double phase_start_epoch;
} RotatorQueueItem;

typedef struct
{
    RotatorQueueItem items[MAX_ROTATOR_QUEUE];
    int count;
    int active_idx;
} RotatorQueue;

void RotatorShutdown(void);

// RotatorState is module-private
struct RotatorState;

typedef struct RotatorDriver
{
    const char *key;
    const char *label;
    // true keeps the unwrapped wire az, false folds to 0-360
    bool az_continuous;

    bool (*connect)(struct RotatorState *st);
    void (*disconnect)(struct RotatorState *st);
    bool (*get_position)(struct RotatorState *st, float *az, float *el);
    bool (*set_target)(struct RotatorState *st, float az, float el);
    bool (*park)(struct RotatorState *st, float az, float el);
    bool (*stop)(struct RotatorState *st);
} RotatorDriver;

// registered protocol drivers, at least one
int RotatorProtocolCount(void);
const char *RotatorProtocolKey(int idx);
const char *RotatorProtocolLabel(int idx);
const char *RotatorGetProtocol(void);

// park through the guarded funnel, no-op when disconnected
void RotatorParkNow(void);

void RotatorSaveSettings(AppConfig *cfg);
void RotatorLoadSettings(const AppConfig *cfg);

// the queue survives a restart, config.cpp owns the block
void RotatorSerializeQueue(nlohmann::json &out);
// terminal states are not restored, RQ_PENDING items get re-armed on resolve
void RotatorDecodeQueue(const nlohmann::json &in);

int RotatorGetHostBufferSize(void);
int RotatorGetPortBufferSize(void);
int RotatorGetGetFmtBufferSize(void);
int RotatorGetSetFmtBufferSize(void);
int RotatorGetCustomCmdBufferSize(void);
int RotatorGetParkAzBufferSize(void);
int RotatorGetParkElBufferSize(void);
int RotatorGetLeadTimeBufferSize(void);
bool RotatorGetAutoSteer(void);
void RotatorSetAutoSteer(bool enabled);
int RotatorGetLeadTimeSec(void);
int RotatorGetLosHoldBufferSize(void);
int RotatorGetLosHoldSec(void);
bool RotatorGetParkBetweenPasses(void);
bool RotatorGetQueueTracking(void);
// seconds left in the post-LOS hold, 0 when not holding
double RotatorGetLosHoldRemaining(double now_epoch);
void RotatorConnect(void);
void RotatorDisconnect(void);
void RotatorPollNow(void);
void RotatorSendCustomNow(void);

void RotatorQueueClear(void);
// add if absent (true), remove if present (false)
bool RotatorQueueToggle(uint32_t norad, double aos, double los, float max_el);
bool RotatorQueueIsQueued(uint32_t norad, double aos);
int RotatorQueueCount(void);
// NULL when empty
const RotatorQueueItem *RotatorQueueItems(void);
int RotatorQueueActiveIndex(void);

// re-bind to live passes by (norad, AOS), prune and re-sort, returns the count
int RotatorQueueResolve(const struct SatPass *passes_in, int pass_count, double now);

// when stopped RotatorTick() only polls
void RotatorQueueStart(void);
void RotatorQueueStop(void);

// advance to the next armed item, returns new index or -1
int RotatorQueueAdvance(int current_idx, double now);

void RotatorTick(const struct SatPass *passes_in, int pass_count, double now);

bool RotatorIsConnected(void);
float RotatorGetAz(void);
float RotatorGetEl(void);

bool RotatorHasPosition(void);
const char *RotatorGetStatusText(void);
bool RotatorCommLost(void);
bool RotatorQueueIsRunning(void);

// last heading commanded through the funnel, false when none sent yet
bool RotatorGetCommandedTarget(float *az_out, float *el_out);

// only public motion entry point, routes through RotatorCommandSlew
void RotatorGotoAzEl(float az, float el);

#endif
