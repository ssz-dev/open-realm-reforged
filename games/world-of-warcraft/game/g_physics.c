#include "g_wow_local.h"
#include <math.h>

/* Ground state is updated only here so every living entity resolves the same floor contract. */
static void Wow_PhysicsSetGround(wowEntityLocal_t *local, LPCWOWGROUNDRESULT ground) {
    local->grounded = true;
    local->vertical_velocity = 0.0f;
    local->ground_height = ground->height;
    local->ground_normal = ground->normal;
    local->ground_surface = ground->surface;
}

/* Air state preserves velocity across frames without inventing a second position. */
static void Wow_PhysicsSetAirborne(wowEntityLocal_t *local) {
    local->grounded = false;
    local->ground_surface = WOW_SURFACE_NONE;
}

/* One bounded substep handles slope, step, snap, and analytic gravity integration. */
static BOOL Wow_PhysicsStep(LPEDICT ent, LPCVECTOR2 displacement, FLOAT seconds) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    VECTOR3 start = ent->s.origin;
    WOWGROUNDQUERY query = {
        .origin = { start.x + displacement->x, start.y + displacement->y, start.z },
        .max_down = BZ_WOW_GROUND_QUERY_DOWN,
        .max_up = BZ_WOW_GROUND_BLOCK_UP,
    };
    WOWGROUNDRESULT ground;
    BOOL has_ground = CM_WowQueryGround(&query, &ground);
    BOOL horizontal = fabsf(displacement->x) > 0.000001f || fabsf(displacement->y) > 0.000001f;
    FLOAT rise = has_ground ? ground.height - start.z : 0.0f;

    if (has_ground && (rise > BZ_WOW_STEP_HEIGHT || ground.normal.z < BZ_WOW_MAX_SLOPE_Z)) {
        /* A heightfield cliff is solid: the old endpoint snap crossed it or embedded the entity. */
        if (local->grounded) return false;
        has_ground = false;
    } else {
        ent->s.origin.x = query.origin.x;
        ent->s.origin.y = query.origin.y;
    }
    if (has_ground && local->grounded && rise >= -BZ_WOW_GROUND_SNAP_DOWN) {
        ent->s.origin.z = ground.height;
        Wow_PhysicsSetGround(local, &ground);
        ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
        return horizontal;
    }

    if (local->grounded) {
        local->vertical_velocity = MIN(local->vertical_velocity, 0.0f);
        Wow_PhysicsSetAirborne(local);
    }
    ent->s.origin.z = start.z + local->vertical_velocity * seconds -
                      0.5f * BZ_WOW_GRAVITY * seconds * seconds;
    /* Terminal velocity bounds long missing-floor spans; the old hard snap had no airborne path. */
    local->vertical_velocity = MAX(local->vertical_velocity - BZ_WOW_GRAVITY * seconds,
                                   -BZ_WOW_TERMINAL_VELOCITY);
    if (has_ground && ent->s.origin.z <= ground.height) {
        ent->s.origin.z = ground.height;
        Wow_PhysicsSetGround(local, &ground);
    }
    ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
    return horizontal;
}

/* Spawn, respawn, and explicit relocation share the same bounded floor selection. */
BOOL Wow_PlaceEntityOnGround(LPEDICT ent, LPCVECTOR3 position) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    WOWGROUNDQUERY query;
    WOWGROUNDRESULT ground;

    if (!ent || !local || !position) return false;
    query = (WOWGROUNDQUERY){
        .origin = *position,
        .max_down = BZ_WOW_GROUND_PLACE_DOWN,
        .max_up = BZ_WOW_GROUND_PLACE_UP,
    };
    if (!CM_WowQueryGround(&query, &ground)) return false;
    ent->s.origin = *position;
    ent->s.origin2 = (VECTOR2){ position->x, position->y };
    ent->s.origin.z = ground.height;
    Wow_PhysicsSetGround(local, &ground);
    return true;
}

/* Bounded substeps make slope and gravity results stable across normal frame-time changes. */
BOOL Wow_MoveEntity(LPEDICT ent, LPCVECTOR2 displacement, FLOAT seconds) {
    static BOOL warned_invalid;
    static BOOL warned_large;
    FLOAT length, countf;
    DWORD count;
    VECTOR2 step;
    BOOL progressed = false;

    if (!ent || !Wow_EntityLocal(ent) || !displacement || !isfinite(displacement->x) ||
        !isfinite(displacement->y) || !isfinite(seconds) || seconds <= 0.0f) {
        if (!warned_invalid) {
            fprintf(stderr, "OpenWoW physics: rejected invalid movement input\n");
            warned_invalid = true;
        }
        return false;
    }
    length = Vector2_len(displacement);
    countf = MAX(ceilf(length / BZ_WOW_MOVE_SUBSTEP), ceilf(seconds / BZ_WOW_TIME_SUBSTEP));
    if (countf > BZ_WOW_MAX_MOVE_SUBSTEPS) {
        if (!warned_large) {
            fprintf(stderr, "OpenWoW physics: rejected movement requiring %.0f substeps\n", (double)countf);
            warned_large = true;
        }
        return false;
    }
    count = MAX(1u, (DWORD)countf);
    step = Vector2_scale(displacement, 1.0f / (FLOAT)count);
    FOR_LOOP(i, count)
        progressed |= Wow_PhysicsStep(ent, &step, seconds / (FLOAT)count);
    return progressed;
}
