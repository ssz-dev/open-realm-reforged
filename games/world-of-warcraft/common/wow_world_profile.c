#include "common/wow_world_query.h"
#include <stdio.h>
#include <string.h>

static WOWWORLDPROFILE cm_wow_world_profile;

/* Saturation keeps an intentionally long developer capture bounded and monotonic. */
static ULONGLONG CM_WowWorldProfileSum(ULONGLONG value, ULONGLONG amount) {
    return amount > ~0ull - value ? ~0ull : value + amount;
}

void CM_WowWorldProfileInit(BOOL enabled) {
    memset(&cm_wow_world_profile, 0, sizeof(cm_wow_world_profile));
    cm_wow_world_profile.enabled = enabled;
}

void CM_WowWorldProfileEnable(BOOL enabled) { cm_wow_world_profile.enabled = enabled; }
BOOL CM_WowWorldProfileEnabled(void) { return cm_wow_world_profile.enabled; }

/* Reset preserves the observed developer budget so the next interval can be compared with its baseline. */
void CM_WowWorldProfileReset(void) {
    BOOL enabled = cm_wow_world_profile.enabled;
    BOOL budget_valid = cm_wow_world_profile.budget_valid;
    ULONGLONG budget_candidates = cm_wow_world_profile.budget_candidates;
    ULONGLONG budget_iterations = cm_wow_world_profile.budget_iterations;
    ULONGLONG budget_lru_lookups = cm_wow_world_profile.budget_lru_lookups;
    ULONGLONG budget_lru_misses = cm_wow_world_profile.budget_lru_misses;

    memset(&cm_wow_world_profile, 0, sizeof(cm_wow_world_profile));
    cm_wow_world_profile.enabled = enabled;
    cm_wow_world_profile.budget_valid = budget_valid;
    cm_wow_world_profile.budget_candidates = budget_candidates;
    cm_wow_world_profile.budget_iterations = budget_iterations;
    cm_wow_world_profile.budget_lru_lookups = budget_lru_lookups;
    cm_wow_world_profile.budget_lru_misses = budget_lru_misses;
}

/* All producers enter through one saturating counter path; disabled profiling changes no query data. */
void CM_WowWorldProfileAdd(wowWorldProfileCounter_t counter, ULONGLONG amount) {
    ULONGLONG *value;

    if (!cm_wow_world_profile.enabled || counter >= WOW_WORLD_PROFILE_COUNTER_COUNT || !amount) return;
    value = &cm_wow_world_profile.counters[counter];
    if (counter == WOW_WORLD_PROFILE_MAX_CANDIDATES || counter == WOW_WORLD_PROFILE_MAX_ITERATIONS)
        *value = MAX(*value, amount);
    else
        *value = CM_WowWorldProfileSum(*value, amount);
    if (counter == WOW_WORLD_PROFILE_WMO_BROADPHASE_CANDIDATES)
        cm_wow_world_profile.current_candidates =
            CM_WowWorldProfileSum(cm_wow_world_profile.current_candidates, amount);
    else if (counter == WOW_WORLD_PROFILE_TRIANGLE_TESTS)
        cm_wow_world_profile.current_iterations =
            CM_WowWorldProfileSum(cm_wow_world_profile.current_iterations, amount);
}

/* Query-local peaks share the central structure because world queries currently run on the server thread. */
void CM_WowWorldProfileBeginQuery(void) {
    if (!cm_wow_world_profile.enabled) return;
    cm_wow_world_profile.current_candidates = 0;
    cm_wow_world_profile.current_iterations = 0;
}

void CM_WowWorldProfileEndQuery(void) {
    if (!cm_wow_world_profile.enabled) return;
    CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_MAX_CANDIDATES, cm_wow_world_profile.current_candidates);
    CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_MAX_ITERATIONS, cm_wow_world_profile.current_iterations);
    cm_wow_world_profile.current_candidates = 0;
    cm_wow_world_profile.current_iterations = 0;
}

void CM_WowWorldProfileSnapshot(LPWOWWORLDPROFILE result) {
    if (result) *result = cm_wow_world_profile;
}

/* Summaries establish an observed baseline first, then flag only later intervals that exceed that workload. */
void CM_WowWorldProfilePrint(void) {
    ULONGLONG *c = cm_wow_world_profile.counters;
    double triangles_per_sweep = c[WOW_WORLD_PROFILE_WORLD_SWEEPS]
        ? (double)c[WOW_WORLD_PROFILE_TRIANGLE_TESTS] / (double)c[WOW_WORLD_PROFILE_WORLD_SWEEPS] : 0.0;
    double miss_percent = c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS]
        ? 100.0 * (double)c[WOW_WORLD_PROFILE_TERRAIN_LRU_MISSES] /
            (double)c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS] : 0.0;

    fprintf(stderr,
            "OpenWoW world profile: enabled=%u ground=%llu chunk=%llu hit=%llu miss=%llu (%.2f%%) sweeps=%llu\n"
            "  WMO candidates=%llu instances=%llu groups=%llu triangles=%llu (%.2f/sweep) peak=%llu/%llu\n"
            "  camera=%llu clamp=%llu LOS=%llu blocked=%llu projectile=%llu wall=%llu\n"
            "  steering=%llu recover=%llu evade=%llu distance=%llu\n",
            (unsigned)cm_wow_world_profile.enabled,
            c[WOW_WORLD_PROFILE_GROUND_QUERIES], c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS],
            c[WOW_WORLD_PROFILE_TERRAIN_LRU_HITS], c[WOW_WORLD_PROFILE_TERRAIN_LRU_MISSES], miss_percent,
            c[WOW_WORLD_PROFILE_WORLD_SWEEPS], c[WOW_WORLD_PROFILE_WMO_BROADPHASE_CANDIDATES],
            c[WOW_WORLD_PROFILE_WMO_INSTANCE_TESTS], c[WOW_WORLD_PROFILE_WMO_GROUP_TESTS],
            c[WOW_WORLD_PROFILE_TRIANGLE_TESTS], triangles_per_sweep,
            c[WOW_WORLD_PROFILE_MAX_CANDIDATES], c[WOW_WORLD_PROFILE_MAX_ITERATIONS],
            c[WOW_WORLD_PROFILE_CAMERA_SWEEPS], c[WOW_WORLD_PROFILE_CAMERA_CLAMPS],
            c[WOW_WORLD_PROFILE_LOS_QUERIES], c[WOW_WORLD_PROFILE_BLOCKED_LOS_QUERIES],
            c[WOW_WORLD_PROFILE_PROJECTILE_SWEEPS], c[WOW_WORLD_PROFILE_PROJECTILE_WALL_HITS],
            c[WOW_WORLD_PROFILE_AI_STEERING_PROBES], c[WOW_WORLD_PROFILE_STUCK_RECOVERIES],
            c[WOW_WORLD_PROFILE_EVADE_FALLBACKS], c[WOW_WORLD_PROFILE_DISTANCE_REJECTS]);

    if (!cm_wow_world_profile.enabled) return;
    if (!cm_wow_world_profile.budget_valid &&
        (c[WOW_WORLD_PROFILE_GROUND_QUERIES] || c[WOW_WORLD_PROFILE_WORLD_SWEEPS])) {
        cm_wow_world_profile.budget_valid = true;
        cm_wow_world_profile.budget_candidates = c[WOW_WORLD_PROFILE_MAX_CANDIDATES];
        cm_wow_world_profile.budget_iterations = c[WOW_WORLD_PROFILE_MAX_ITERATIONS];
        cm_wow_world_profile.budget_lru_lookups = c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS];
        cm_wow_world_profile.budget_lru_misses = c[WOW_WORLD_PROFILE_TERRAIN_LRU_MISSES];
        fprintf(stderr, "  budget baseline captured from this interval\n");
        return;
    }
    if (!cm_wow_world_profile.budget_valid) return;
    if (c[WOW_WORLD_PROFILE_MAX_CANDIDATES] > cm_wow_world_profile.budget_candidates)
        fprintf(stderr, "  budget warning: WMO candidate peak exceeded observed baseline (%llu > %llu)\n",
                c[WOW_WORLD_PROFILE_MAX_CANDIDATES], cm_wow_world_profile.budget_candidates);
    if (c[WOW_WORLD_PROFILE_MAX_ITERATIONS] > cm_wow_world_profile.budget_iterations)
        fprintf(stderr, "  budget warning: triangle iteration peak exceeded observed baseline (%llu > %llu)\n",
                c[WOW_WORLD_PROFILE_MAX_ITERATIONS], cm_wow_world_profile.budget_iterations);
    if (cm_wow_world_profile.budget_lru_lookups && c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS] &&
        (long double)c[WOW_WORLD_PROFILE_TERRAIN_LRU_MISSES] /
            (long double)c[WOW_WORLD_PROFILE_TERRAIN_CHUNK_LOOKUPS] >
        (long double)cm_wow_world_profile.budget_lru_misses /
            (long double)cm_wow_world_profile.budget_lru_lookups)
        fprintf(stderr, "  budget warning: terrain LRU miss ratio exceeded observed baseline\n");
}
