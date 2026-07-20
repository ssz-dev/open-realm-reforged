#include "test_framework.h"

#include <math.h>
#include <string.h>

#include "game/g_wow_local.h"

int _tests_run;
int _tests_failed;

struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];

typedef enum {
    TEST_GROUND_FLAT,
    TEST_GROUND_PLANE,
    TEST_GROUND_STEP,
    TEST_GROUND_NONE,
} testGroundType_t;

typedef enum {
    TEST_COLLISION_NONE,
    TEST_COLLISION_WALL,
    TEST_COLLISION_CORNER,
    TEST_COLLISION_DOOR,
} testCollisionType_t;

typedef struct {
    testGroundType_t type;
    FLOAT base;
    FLOAT slope_x;
    FLOAT slope_y;
    FLOAT step_x;
    FLOAT step_height;
} TESTGROUND;

static TESTGROUND test_ground;
static testCollisionType_t test_collision;
static DWORD test_sweep_calls;

DWORD Wow_EntityIndex(LPCEDICT ent) {
    return ent >= wow_edicts && ent < wow_edicts + WOW_MAX_EDICTS
        ? (DWORD)(ent - wow_edicts) : WOW_MAX_EDICTS;
}

wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent) {
    DWORD index = Wow_EntityIndex(ent);
    return index < WOW_MAX_EDICTS ? &wow_entity_locals[index] : NULL;
}

/* Artificial analytic surfaces exercise physics without depending on game archives. */
BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result) {
    FLOAT height;

    if (!query || !result || test_ground.type == TEST_GROUND_NONE) return false;
    height = test_ground.base + test_ground.slope_x * query->origin.x +
             test_ground.slope_y * query->origin.y;
    if (test_ground.type == TEST_GROUND_STEP && query->origin.x >= test_ground.step_x)
        height += test_ground.step_height;
    if (height < query->origin.z - query->max_down || height > query->origin.z + query->max_up)
        return false;
    *result = (WOWGROUNDRESULT){
        .height = height,
        .normal = { -test_ground.slope_x, -test_ground.slope_y, 1.0f },
        .surface = WOW_SURFACE_TERRAIN,
    };
    Vector3_normalize(&result->normal);
    return true;
}

static void test_sweep_plane(LPCWOWSWEEPQUERY query, BOOL y_axis, LPWOWSWEEPRESULT result) {
    FLOAT start = y_axis ? query->start.y : query->start.x;
    FLOAT movement = y_axis ? query->displacement.y : query->displacement.x;
    FLOAT other_start = y_axis ? query->start.x : query->start.y;
    FLOAT other_move = y_axis ? query->displacement.x : query->displacement.y;
    FLOAT wall = 1.0f, distance = start - wall;
    FLOAT fraction, other;
    VECTOR3 normal = y_axis ? (VECTOR3){ 0.0f, -1.0f, 0.0f } : (VECTOR3){ -1.0f, 0.0f, 0.0f };

    if (fabsf(distance) < query->radius) {
        if (test_collision == TEST_COLLISION_DOOR && !y_axis && fabsf(other_start) < 0.75f) return;
        result->start_solid = true;
        result->fraction = 0.0f;
        result->penetration = query->radius - fabsf(distance);
        if (distance > 0.0f) normal = Vector3_scale(&normal, -1.0f);
        result->normal = normal;
        result->surface = WOW_SURFACE_WMO;
        return;
    }
    if (movement > 0.0f && start <= wall - query->radius)
        fraction = (wall - query->radius - start) / movement;
    else if (movement < 0.0f && start >= wall + query->radius)
        fraction = (wall + query->radius - start) / movement, normal = Vector3_scale(&normal, -1.0f);
    else
        return;
    if (fraction < 0.0f || fraction > result->fraction || fraction > 1.0f) return;
    other = other_start + other_move * fraction;
    if (test_collision == TEST_COLLISION_DOOR && !y_axis && fabsf(other) < 0.75f) return;
    result->fraction = fraction;
    result->normal = normal;
    result->surface = WOW_SURFACE_WMO;
}

/* Artificial zero-thickness walls test gameplay sweep/slide without archive geometry. */
BOOL CM_WowSweepWorld(LPCWOWSWEEPQUERY query, LPWOWSWEEPRESULT result) {
    test_sweep_calls++;
    *result = (WOWSWEEPRESULT){ .fraction = 1.0f };
    if (test_collision == TEST_COLLISION_NONE) {
        result->end = Vector3_add(&query->start, &query->displacement);
        return false;
    }
    test_sweep_plane(query, false, result);
    if (test_collision == TEST_COLLISION_CORNER && !result->start_solid)
        test_sweep_plane(query, true, result);
    result->end = Vector3_mad(&query->start, result->fraction, &query->displacement);
    return result->start_solid || result->fraction < 1.0f;
}

static LPEDICT test_entity(wowEntityKind_t kind, VECTOR3 position) {
    LPEDICT ent = &wow_edicts[0];

    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    ent->inuse = true;
    ent->s.number = 0;
    ent->s.origin = position;
    ent->s.origin2 = (VECTOR2){ position.x, position.y };
    ent->s.radius = 0.5f;
    Wow_EntityLocal(ent)->kind = kind;
    return ent;
}

static void test_reset_ground(void) {
    test_ground = (TESTGROUND){ .type = TEST_GROUND_FLAT };
    test_collision = TEST_COLLISION_NONE;
    test_sweep_calls = 0;
}

static void test_flat_ground_places_player(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 2.0f, 3.0f, 4.0f });
    test_ground.base = 1.0f;

    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT_EQ_FLOAT(ent->s.origin.z, 1.0f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
    ASSERT_EQ_INT(Wow_EntityLocal(ent)->ground_surface, WOW_SURFACE_TERRAIN);
}

static void test_walkable_uphill_and_downhill_stay_grounded(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    test_ground.type = TEST_GROUND_PLANE;
    test_ground.slope_x = 0.25f;

    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ 1.0f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.z, 0.25f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ -1.0f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.z, 0.0f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
}

static void test_steep_slope_blocks_horizontal_motion(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    test_ground.type = TEST_GROUND_PLANE;
    test_ground.slope_x = 2.0f;

    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 0.5f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.x, 0.0f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
}

static void test_small_step_passes_and_high_step_blocks(void) {
    LPEDICT ent;

    test_ground.type = TEST_GROUND_STEP;
    test_ground.step_x = 0.25f;
    test_ground.step_height = 0.5f;
    ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ 0.5f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.z, 0.5f, 0.0001f);

    test_ground.step_height = 1.5f;
    ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 0.5f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.x, 0.0f, 0.0001f);
}

static void test_snap_tolerance_and_surface_above_entity(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.5f });
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    local->grounded = true;
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.z, 0.0f, 0.0001f);
    ASSERT(local->grounded);

    ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    test_ground.base = 4.0f;
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, 0.1f));
    ASSERT(ent->s.origin.z < 0.0f);
    ASSERT(!Wow_EntityLocal(ent)->grounded);
}

static void test_missing_ground_falls_and_lands_when_floor_returns(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 0.0f, 0.0f, 2.0f });
    VECTOR3 before = ent->s.origin;

    test_ground.type = TEST_GROUND_NONE;
    ASSERT(!Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT_EQ_FLOAT(ent->s.origin.z, before.z, 0.0001f);
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, 0.1f));
    ASSERT(ent->s.origin.z < 2.0f);
    ASSERT(!Wow_EntityLocal(ent)->grounded);
    test_ground.type = TEST_GROUND_FLAT;
    FOR_LOOP(i, 10)
        (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, 0.1f);
    ASSERT_EQ_FLOAT(ent->s.origin.z, 0.0f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
}

static void test_player_and_creature_share_uneven_grounding(void) {
    LPEDICT player;
    LPEDICT creature;

    test_ground.type = TEST_GROUND_PLANE;
    test_ground.slope_y = -0.2f;
    player = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(player, &player->s.origin));
    ASSERT(Wow_MoveEntity(player, &(VECTOR2){ 0.0f, 1.0f }, 0.1f));
    ASSERT_EQ_FLOAT(player->s.origin.z, -0.2f, 0.0001f);

    creature = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(creature, &creature->s.origin));
    ASSERT(Wow_MoveEntity(creature, &(VECTOR2){ 0.0f, 1.0f }, 0.1f));
    ASSERT_EQ_FLOAT(creature->s.origin.z, -0.2f, 0.0001f);
    ASSERT(Wow_EntityLocal(creature)->grounded);
}

static void test_respawn_style_placement_reuses_ground_query(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 8.0f, 4.0f, 20.0f });
    VECTOR3 home = { 3.0f, 2.0f, 10.0f };

    test_ground.base = 6.0f;
    ASSERT(Wow_PlaceEntityOnGround(ent, &home));
    ASSERT_EQ_FLOAT(ent->s.origin.x, 3.0f, 0.0001f);
    ASSERT_EQ_FLOAT(ent->s.origin.z, 6.0f, 0.0001f);
    ASSERT(Wow_EntityLocal(ent)->grounded);
}

static void test_frame_times_produce_nearly_identical_fall(void) {
    LPEDICT ent_a = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 20.0f });
    VECTOR3 result_a;
    LPEDICT ent_b;

    test_ground.type = TEST_GROUND_NONE;
    FOR_LOOP(i, 10)
        (void)Wow_MoveEntity(ent_a, &(VECTOR2){ 0.1f, 0.0f }, 0.1f);
    result_a = ent_a->s.origin;

    ent_b = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 20.0f });
    FOR_LOOP(i, 20)
        (void)Wow_MoveEntity(ent_b, &(VECTOR2){ 0.05f, 0.0f }, 0.05f);
    ASSERT_EQ_FLOAT(ent_b->s.origin.x, result_a.x, 0.0001f);
    ASSERT_EQ_FLOAT(ent_b->s.origin.z, result_a.z, 0.0001f);
}

static void test_positions_remain_finite_without_ground(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 0.0f, 0.0f, 5.0f });

    test_ground.type = TEST_GROUND_NONE;
    FOR_LOOP(i, 100)
        (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.02f, -0.01f }, 0.1f);
    ASSERT(isfinite(ent->s.origin.x));
    ASSERT(isfinite(ent->s.origin.y));
    ASSERT(isfinite(ent->s.origin.z));
    ASSERT(isfinite(Wow_EntityLocal(ent)->vertical_velocity));
    ASSERT_EQ_FLOAT(Wow_EntityLocal(ent)->vertical_velocity, -BZ_WOW_TERMINAL_VELOCITY, 0.0001f);
}

static void test_invalid_and_oversized_motion_preserve_position(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 1.0f, 2.0f, 3.0f });
    VECTOR3 before = ent->s.origin;

    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ NAN, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.x, before.x, 0.0001f);
    ASSERT_EQ_FLOAT(ent->s.origin.z, before.z, 0.0001f);
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 1000.0f, 0.0f }, 0.1f));
    ASSERT_EQ_FLOAT(ent->s.origin.x, before.x, 0.0001f);
    ASSERT_EQ_FLOAT(ent->s.origin.z, before.z, 0.0001f);
}

static void test_wall_blocks_and_diagonal_motion_slides(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });

    test_collision = TEST_COLLISION_WALL;
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ 2.0f, 1.0f }, 0.2f));
    ASSERT(ent->s.origin.x < 0.5f);
    ASSERT(ent->s.origin.y > 0.8f);
    ASSERT(test_sweep_calls > 0);
}

static void test_corner_blocks_both_axes_without_jitter(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    VECTOR3 stopped;

    test_collision = TEST_COLLISION_CORNER;
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    (void)Wow_MoveEntity(ent, &(VECTOR2){ 2.0f, 2.0f }, 0.2f);
    stopped = ent->s.origin;
    ASSERT(stopped.x < 0.5f && stopped.y < 0.5f);
    FOR_LOOP(i, 5) (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.1f, 0.1f }, 0.1f);
    ASSERT_EQ_FLOAT(ent->s.origin.x, stopped.x, 0.02f);
    ASSERT_EQ_FLOAT(ent->s.origin.y, stopped.y, 0.02f);
}

static void test_open_door_and_thin_wall_sweep_have_distinct_results(void) {
    LPEDICT ent;

    test_collision = TEST_COLLISION_DOOR;
    ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ 4.0f, 0.0f }, 0.2f));
    ASSERT_EQ_FLOAT(ent->s.origin.x, 4.0f, 0.02f);

    test_collision = TEST_COLLISION_WALL;
    ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    (void)Wow_MoveEntity(ent, &(VECTOR2){ 4.0f, 0.0f }, 0.2f);
    ASSERT(ent->s.origin.x < 0.5f);
}

static void test_start_inside_wall_depenetrates_and_stays_finite(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.8f, 0.0f, 0.0f });

    ASSERT(Wow_PlaceEntityOnGround(ent, &ent->s.origin));
    test_collision = TEST_COLLISION_WALL;
    ASSERT(Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, 0.1f));
    ASSERT(ent->s.origin.x < 0.41f);
    ASSERT(isfinite(ent->s.origin.x));
}

static void test_player_and_creature_use_same_wall_pipeline(void) {
    LPEDICT player;
    LPEDICT creature;

    test_collision = TEST_COLLISION_WALL;
    player = test_entity(WOW_ENTITY_PLAYER, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(player, &player->s.origin));
    (void)Wow_MoveEntity(player, &(VECTOR2){ 2.0f, 0.0f }, 0.1f);
    ASSERT(player->s.origin.x < 0.5f);

    creature = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    ASSERT(Wow_PlaceEntityOnGround(creature, &creature->s.origin));
    (void)Wow_MoveEntity(creature, &(VECTOR2){ 2.0f, 0.0f }, 0.1f);
    ASSERT(creature->s.origin.x < 0.6f);
}

static void test_dead_creature_does_not_query_world_collision(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ 0.0f, 0.0f, 0.0f });

    test_collision = TEST_COLLISION_WALL;
    Wow_EntityLocal(ent)->dead = true;
    ASSERT(!Wow_MoveEntity(ent, &(VECTOR2){ 1.0f, 0.0f }, 0.1f));
    ASSERT_EQ_INT((int)test_sweep_calls, 0);
}

static void test_respawn_style_placement_rejects_wall_overlap(void) {
    LPEDICT ent = test_entity(WOW_ENTITY_CREATURE, (VECTOR3){ -2.0f, 0.0f, 0.0f });
    VECTOR3 blocked = { 0.8f, 0.0f, 0.0f };

    test_collision = TEST_COLLISION_WALL;
    ASSERT(!Wow_PlaceEntityOnGround(ent, &blocked));
    ASSERT_EQ_FLOAT(ent->s.origin.x, -2.0f, 0.001f);
    ASSERT(test_sweep_calls > 0);
}

int main(void) {
    test_reset_ground(); RUN_TEST(test_flat_ground_places_player);
    test_reset_ground(); RUN_TEST(test_walkable_uphill_and_downhill_stay_grounded);
    test_reset_ground(); RUN_TEST(test_steep_slope_blocks_horizontal_motion);
    test_reset_ground(); RUN_TEST(test_small_step_passes_and_high_step_blocks);
    test_reset_ground(); RUN_TEST(test_snap_tolerance_and_surface_above_entity);
    test_reset_ground(); RUN_TEST(test_missing_ground_falls_and_lands_when_floor_returns);
    test_reset_ground(); RUN_TEST(test_player_and_creature_share_uneven_grounding);
    test_reset_ground(); RUN_TEST(test_respawn_style_placement_reuses_ground_query);
    test_reset_ground(); RUN_TEST(test_frame_times_produce_nearly_identical_fall);
    test_reset_ground(); RUN_TEST(test_positions_remain_finite_without_ground);
    test_reset_ground(); RUN_TEST(test_invalid_and_oversized_motion_preserve_position);
    test_reset_ground(); RUN_TEST(test_wall_blocks_and_diagonal_motion_slides);
    test_reset_ground(); RUN_TEST(test_corner_blocks_both_axes_without_jitter);
    test_reset_ground(); RUN_TEST(test_open_door_and_thin_wall_sweep_have_distinct_results);
    test_reset_ground(); RUN_TEST(test_start_inside_wall_depenetrates_and_stays_finite);
    test_reset_ground(); RUN_TEST(test_player_and_creature_use_same_wall_pipeline);
    test_reset_ground(); RUN_TEST(test_dead_creature_does_not_query_world_collision);
    test_reset_ground(); RUN_TEST(test_respawn_style_placement_rejects_wall_overlap);
    TEST_RESULTS();
}
