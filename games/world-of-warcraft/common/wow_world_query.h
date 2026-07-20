#ifndef WOW_WORLD_QUERY_H
#define WOW_WORLD_QUERY_H

#include "common/shared.h"

typedef enum {
    WOW_SURFACE_NONE,
    WOW_SURFACE_TERRAIN,
    WOW_SURFACE_WMO,
    WOW_SURFACE_DOODAD,
} wowSurfaceType_t;

typedef struct {
    VECTOR3 origin;
    FLOAT max_down;
    FLOAT max_up;
    FLOAT max_walkable_up;
} WOWGROUNDQUERY;
typedef WOWGROUNDQUERY *LPWOWGROUNDQUERY;
typedef WOWGROUNDQUERY const *LPCWOWGROUNDQUERY;

typedef struct {
    FLOAT height;
    VECTOR3 normal;
    wowSurfaceType_t surface;
} WOWGROUNDRESULT;
typedef WOWGROUNDRESULT *LPWOWGROUNDRESULT;
typedef WOWGROUNDRESULT const *LPCWOWGROUNDRESULT;

typedef enum {
    WOW_SWEEP_MOVEMENT,
    WOW_SWEEP_CAMERA,
} wowWorldSweepMode_t;

typedef struct {
    VECTOR3 start;
    VECTOR3 displacement;
    FLOAT radius;
    FLOAT height;
    wowWorldSweepMode_t mode;
} WOWSWEEPQUERY;
typedef WOWSWEEPQUERY *LPWOWSWEEPQUERY;
typedef WOWSWEEPQUERY const *LPCWOWSWEEPQUERY;

typedef struct {
    VECTOR3 end;
    VECTOR3 normal;
    FLOAT fraction;
    FLOAT penetration;
    wowSurfaceType_t surface;
    BOOL start_solid;
} WOWSWEEPRESULT;
typedef WOWSWEEPRESULT *LPWOWSWEEPRESULT;
typedef WOWSWEEPRESULT const *LPCWOWSWEEPRESULT;

typedef enum {
    WOW_WORLD_PROFILE_GROUND_QUERIES,
    WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS,
    WOW_WORLD_PROFILE_TERRAIN_LRU_HITS,
    WOW_WORLD_PROFILE_TERRAIN_LRU_MISSES,
    WOW_WORLD_PROFILE_WORLD_SWEEPS,
    WOW_WORLD_PROFILE_WMO_BROADPHASE_CANDIDATES,
    WOW_WORLD_PROFILE_WMO_INSTANCE_TESTS,
    WOW_WORLD_PROFILE_WMO_GROUP_TESTS,
    WOW_WORLD_PROFILE_TRIANGLE_TESTS,
    WOW_WORLD_PROFILE_CAMERA_SWEEPS,
    WOW_WORLD_PROFILE_CAMERA_CLAMPS,
    WOW_WORLD_PROFILE_LOS_QUERIES,
    WOW_WORLD_PROFILE_BLOCKED_LOS_QUERIES,
    WOW_WORLD_PROFILE_PROJECTILE_SWEEPS,
    WOW_WORLD_PROFILE_PROJECTILE_WALL_HITS,
    WOW_WORLD_PROFILE_AI_STEERING_PROBES,
    WOW_WORLD_PROFILE_STUCK_RECOVERIES,
    WOW_WORLD_PROFILE_EVADE_FALLBACKS,
    WOW_WORLD_PROFILE_DISTANCE_REJECTS,
    WOW_WORLD_PROFILE_MAX_CANDIDATES,
    WOW_WORLD_PROFILE_MAX_ITERATIONS,
    WOW_WORLD_PROFILE_COUNTER_COUNT,
} wowWorldProfileCounter_t;

typedef struct {
    BOOL enabled;
    BOOL budget_valid;
    ULONGLONG counters[WOW_WORLD_PROFILE_COUNTER_COUNT];
    ULONGLONG current_candidates;
    ULONGLONG current_iterations;
    ULONGLONG budget_candidates;
    ULONGLONG budget_iterations;
    ULONGLONG budget_lru_lookups;
    ULONGLONG budget_lru_misses;
} WOWWORLDPROFILE;
typedef WOWWORLDPROFILE *LPWOWWORLDPROFILE;
typedef WOWWORLDPROFILE const *LPCWOWWORLDPROFILE;

BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result);
BOOL CM_WowSweepWorld(LPCWOWSWEEPQUERY query, LPWOWSWEEPRESULT result);
void CM_WowWorldProfileInit(BOOL enabled);
void CM_WowWorldProfileEnable(BOOL enabled);
BOOL CM_WowWorldProfileEnabled(void);
void CM_WowWorldProfileReset(void);
void CM_WowWorldProfileAdd(wowWorldProfileCounter_t counter, ULONGLONG amount);
void CM_WowWorldProfileBeginQuery(void);
void CM_WowWorldProfileEndQuery(void);
void CM_WowWorldProfileSnapshot(LPWOWWORLDPROFILE result);
void CM_WowWorldProfilePrint(void);

#endif
