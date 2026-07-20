#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];

static animation_t g_attack_anim = {
    .name = "Attack",
    .interval = { 0, 1000 },
};

static animation_t g_pain_anim = {
    .name = "Pain",
    .interval = { 0, 450 },
};

static animation_t g_death_anim = {
    .name = "Death",
    .interval = { 0, 900 },
};

static DWORD g_pain_calls = 0;
static DWORD g_ground_query_calls;
static DWORD g_world_sweep_calls;
static wowClient_t g_player_client;
static BOOL g_ground_enabled = true;
static FLOAT g_ground_slope_x;
static enum {
    TEST_WORLD_CLEAR,
    TEST_WORLD_BLOCKED,
    TEST_WORLD_WALL_X,
} g_world_mode;

void Wow_SetCombatMessage(LPEDICT player, wowCombatMessageType_t type, DWORD value) {
    (void)player;
    (void)type;
    (void)value;
}

BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result) {
    FLOAT height;

    g_ground_query_calls++;
    if (!g_ground_enabled || !query || !result) return false;
    height = 7.0f + g_ground_slope_x * query->origin.x;
    if (height < query->origin.z - query->max_down || height > query->origin.z + query->max_up)
        return false;
    *result = (WOWGROUNDRESULT){
        .height = height,
        .normal = { -g_ground_slope_x, 0.0f, 1.0f },
        .surface = WOW_SURFACE_TERRAIN,
    };
    Vector3_normalize(&result->normal);
    return true;
}

BOOL CM_WowSweepWorld(LPCWOWSWEEPQUERY query, LPWOWSWEEPRESULT result) {
    FLOAT length;

    g_world_sweep_calls++;
    if (!query || !result) return false;
    *result = (WOWSWEEPRESULT){
        .end = Vector3_add(&query->start, &query->displacement),
        .fraction = 1.0f,
    };
    if (g_world_mode == TEST_WORLD_CLEAR ||
        (g_world_mode == TEST_WORLD_WALL_X && query->displacement.x <= 0.0001f)) return false;
    length = sqrtf(query->displacement.x * query->displacement.x +
                   query->displacement.y * query->displacement.y);
    result->end = query->start;
    result->normal = length > 0.0001f
        ? (VECTOR3){ -query->displacement.x / length, -query->displacement.y / length, 0.0f }
        : (VECTOR3){ -1.0f, 0.0f, 0.0f };
    result->fraction = 0.0f;
    result->surface = WOW_SURFACE_WMO;
    return true;
}

DWORD Wow_EntityIndex(LPCEDICT ent) {
    if (!ent || ent < wow_edicts || ent >= wow_edicts + WOW_MAX_EDICTS) {
        return WOW_MAX_EDICTS;
    }
    return (DWORD)(ent - wow_edicts);
}

wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent) {
    DWORD index = Wow_EntityIndex(ent);

    if (index >= WOW_MAX_EDICTS) {
        return NULL;
    }
    return &wow_entity_locals[index];
}

static LPCANIMATION Test_SelectAnimation(LPCSTR const *animation_names) {
    for (LPCSTR const *name = animation_names; name && *name; name++) {
        if (!strncasecmp(*name, "Attack", 6)) {
            return &g_attack_anim;
        }
        if (!strncasecmp(*name, "CombatWound", 11) ||
            !strncasecmp(*name, "StandWound", 10) ||
            !strncasecmp(*name, "Stun", 4) ||
            !strncasecmp(*name, "Pain", 4)) {
            return &g_pain_anim;
        }
        if (!strncasecmp(*name, "Death", 5) || !strncasecmp(*name, "Dead", 4)) {
            return &g_death_anim;
        }
        if (!strncasecmp(*name, "Stand", 5) || !strncasecmp(*name, "Ready", 5) ||
            !strncasecmp(*name, "Walk", 4) || !strncasecmp(*name, "Run", 3)) {
            return &g_attack_anim;
        }
    }
    return NULL;
}

BOOL Wow_SetEntityMoveFirstAnimation(LPEDICT ent, LPWOWMOVE move, LPCSTR const *animation_names) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPCANIMATION anim;

    if (!ent || !local || !move) {
        return false;
    }

    anim = Test_SelectAnimation(animation_names);
    if (!anim) {
        local->animation = NULL;
        local->currentmove = NULL;
        return false;
    }

    local->currentmove = move;
    local->animation = anim;
    ent->s.frame = anim->interval[0];
    return true;
}

BOOL Wow_SetEntityMove(LPEDICT ent, LPWOWMOVE move) {
    LPCSTR names[2];

    if (!move || !move->animation) {
        return false;
    }
    names[0] = move->animation;
    names[1] = NULL;
    return Wow_SetEntityMoveFirstAnimation(ent, move, names);
}

void Wow_AdvanceEntityFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    DWORD next_frame;

    if (!ent || !local || !local->animation) {
        return;
    }

    next_frame = ent->s.frame + FRAMETIME;
    if (next_frame >= local->animation->interval[1]) {
        ent->s.frame = local->animation->interval[0];
    } else {
        ent->s.frame = next_frame;
    }
}

static void test_pain_stub(LPEDICT ent) {
    (void)ent;
    g_pain_calls++;
}

static void test_reset_world(void) {
    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    memset(&g_player_client, 0, sizeof(g_player_client));
    g_pain_calls = 0;
    g_ground_query_calls = 0;
    g_world_sweep_calls = 0;
    g_ground_enabled = true;
    g_ground_slope_x = 0.0f;
    g_world_mode = TEST_WORLD_CLEAR;
}

static void test_prepare_pair(LPEDICT *attacker_out, LPEDICT *target_out) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;
    wowEntityLocal_t *target_local;

    test_reset_world();

    attacker = &wow_edicts[0];
    target = &wow_edicts[1];
    attacker->inuse = true;
    target->inuse = true;
    attacker->s.number = 0;
    target->s.number = 1;
    attacker->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
    target->s.origin2 = (VECTOR2){ 2.0f, 0.0f };
    attacker->pain = Wow_AIPain;
    attacker->attack = Wow_AIAttack;
    target->pain = test_pain_stub;

    attacker_local = Wow_EntityLocal(attacker);
    target_local = Wow_EntityLocal(target);
    attacker_local->kind = WOW_ENTITY_CREATURE;
    attacker_local->health = 5;
    attacker_local->max_health = 5;
    attacker_local->level = 1;
    attacker_local->hostile = true;
    attacker_local->enemy = target;
    target_local->kind = WOW_ENTITY_CREATURE;
    target_local->health = 3;
    target_local->max_health = 3;
    target_local->level = 1;
    target_local->hostile = true;

    *attacker_out = attacker;
    *target_out = target;
}

/* State-machine tests share one real player and one fully wired creature. */
static void test_prepare_player_creature(LPEDICT *player_out, LPEDICT *creature_out) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;

    test_prepare_pair(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    player->client = &g_player_client.client;
    player->pain = Wow_AIPain;
    player_local->kind = WOW_ENTITY_PLAYER;
    player_local->health = player_local->max_health = BZ_WOW_PLAYER_BASE_HEALTH;
    player_local->max_power = BZ_WOW_PLAYER_MAX_POWER;
    player_local->level = 1;
    player_local->hostile = false;
    player_local->enemy = NULL;
    player->s.origin.z = 7.0f;
    player_local->grounded = true;
    player_local->ground_height = 7.0f;
    player_local->ground_normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
    player_local->ground_surface = WOW_SURFACE_TERRAIN;
    creature->svflags = SVF_MONSTER;
    creature->idle = Wow_AIIdle;
    creature->move = Wow_AIMove;
    creature->run = Wow_AIRunFrame;
    creature->attack = Wow_AIAttack;
    creature->pain = Wow_AIPain;
    creature_local->ai_state = WOW_AI_IDLE;
    creature_local->home = creature->s.origin2;
    creature_local->walk_speed = 2.0f;
    creature_local->attack_damage_point = 200;
    creature_local->attack_backswing = 300;
    creature_local->enemy = NULL;
    creature_local->xp_reward = BZ_WOW_CREATURE_KILL_XP;
    creature->s.origin.z = 7.0f;
    creature_local->home_z = 7.0f;
    creature_local->grounded = true;
    creature_local->ground_height = 7.0f;
    creature_local->ground_normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
    creature_local->ground_surface = WOW_SURFACE_TERRAIN;
    Wow_AIResetNavigation(creature);
    *player_out = player;
    *creature_out = creature;
}

static void test_wow_attack_applies_damage_after_damage_point(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    attacker_local = Wow_EntityLocal(attacker);
    target_local = Wow_EntityLocal(target);

    Wow_AIAttack(attacker);

    ASSERT_EQ_INT((int)target_local->health, 3);
    ASSERT_EQ_INT((int)attacker_local->attack_damage_time, 350);
    ASSERT_EQ_INT((int)attacker_local->attack_backswing_time, 650);

    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 3);
    ASSERT_EQ_INT((int)g_pain_calls, 0);

    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 2);
    ASSERT_EQ_INT((int)g_pain_calls, 1);

    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 2);
    ASSERT_EQ_INT((int)g_pain_calls, 1);
}

static void test_wow_attack_uses_explicit_timing_over_animation_split(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;

    test_prepare_pair(&attacker, &target);
    attacker_local = Wow_EntityLocal(attacker);
    attacker_local->attack_damage_point = 120;
    attacker_local->attack_backswing = 180;

    Wow_AIAttack(attacker);

    ASSERT_EQ_INT((int)attacker_local->attack_damage_time, 120);
    ASSERT_EQ_INT((int)attacker_local->attack_backswing_time, 180);
    ASSERT_EQ_INT((int)attacker_local->attack_time, 300);
}

static void test_wow_swing_requires_target_in_range_at_damage_point(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    Wow_AIAttack(attacker);
    target->s.origin2.x = WOW_MELEE_RANGE + 1.0f;
    FOR_LOOP(i, 4) Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 3);
}

static void test_wow_walls_block_proximity_aggro_and_melee_start(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;
    WOWWORLDPROFILE profile;

    test_prepare_player_creature(&player, &creature);
    CM_WowWorldProfileInit(true);
    local = Wow_EntityLocal(creature);
    creature->s.origin2.x = creature->s.origin.x = 2.0f;
    local->home = creature->s.origin2;
    g_world_mode = TEST_WORLD_BLOCKED;
    ASSERT(!Wow_HasLineOfSight(creature, player));
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_IDLE);
    ASSERT_NULL(local->enemy);

    local->enemy = player;
    local->ai_state = WOW_AI_CHASE;
    Wow_AIAttack(creature);
    ASSERT_EQ_INT((int)local->attack_damage_time, 0);
    ASSERT_EQ_INT((int)local->attack_backswing_time, 0);
    g_world_mode = TEST_WORLD_CLEAR;
    ASSERT(Wow_HasLineOfSight(creature, player));
    Wow_AIAttack(creature);
    ASSERT_EQ_INT((int)local->attack_damage_time, 200);
    ASSERT_EQ_INT((int)local->attack_backswing_time, 300);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT(profile.counters[WOW_WORLD_PROFILE_LOS_QUERIES] >= 5);
    ASSERT(profile.counters[WOW_WORLD_PROFILE_BLOCKED_LOS_QUERIES] >= 3);
    CM_WowWorldProfileEnable(false);
}

static void test_wow_wall_at_damage_point_cancels_melee_hit(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    Wow_AIAttack(attacker);
    g_world_mode = TEST_WORLD_BLOCKED;
    FOR_LOOP(i, 4) Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 3);
    ASSERT_EQ_INT((int)g_pain_calls, 0);
}

static void test_wow_attack_lethal_triggers_death_state(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    target_local->health = 1;

    Wow_AIAttack(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);

    ASSERT(target_local->dead);
    ASSERT_EQ_INT((int)target_local->health, 0);
    ASSERT((target->svflags & SVF_DEADMONSTER) != 0);
}

static void test_wow_dead_entity_ignores_pain_and_attack(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    Wow_AIDie(target, attacker);

    Wow_AIPain(target);
    Wow_AIAttack(target);

    ASSERT(target_local->dead);
    ASSERT_EQ_INT((int)target_local->pain_time, 0);
    ASSERT_EQ_INT((int)target_local->attack_damage_time, 0);
    ASSERT_EQ_INT((int)target_local->attack_backswing_time, 0);
}

static void test_wow_death_holds_terminal_frame(void) {
    LPEDICT attacker;
    LPEDICT target;

    test_prepare_pair(&attacker, &target);
    Wow_AIDie(target, attacker);

    FOR_LOOP(i, 20) {
        (void)Wow_AIAdvanceLockedFrame(target);
    }

    ASSERT_EQ_INT((int)target->s.frame, (int)(g_death_anim.interval[1] - 1));
}

static void test_wow_vitals_publish_living_health_fraction(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *local;

    test_prepare_pair(&attacker, &target);
    local = Wow_EntityLocal(target);
    Wow_SyncEntityVitals(target);
    ASSERT_EQ_INT((int)target->s.stats[ENT_HEALTH], 255);
    local->health = 2;
    Wow_SyncEntityVitals(target);
    ASSERT_EQ_INT((int)target->s.stats[ENT_HEALTH], 170);
    Wow_AIDie(target, attacker);
    ASSERT_EQ_INT((int)target->s.stats[ENT_HEALTH], 0);
}

static void test_wow_player_kill_grants_xp_and_levels_with_overflow(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;

    test_prepare_pair(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    ASSERT_EQ_INT((int)Wow_XpForNextLevel(0), 0);
    ASSERT_EQ_INT((int)Wow_XpForNextLevel(1), 400);
    ASSERT_EQ_INT((int)Wow_XpForNextLevel(2), 900);
    ASSERT_EQ_INT((int)Wow_XpForNextLevel(BZ_WOW_MAX_LEVEL), 0);
    player_local->kind = WOW_ENTITY_PLAYER;
    player_local->health = player_local->max_health = BZ_WOW_PLAYER_BASE_HEALTH;
    player_local->max_power = BZ_WOW_PLAYER_MAX_POWER;
    player_local->level = 1;
    player_local->xp = 350;
    creature_local->xp_reward = BZ_WOW_CREATURE_KILL_XP;
    Wow_AIDie(creature, player);
    ASSERT_EQ_INT((int)player_local->level, 2);
    ASSERT_EQ_INT((int)player_local->xp, 50);
    ASSERT_EQ_INT((int)player_local->max_health, 110);
    ASSERT_EQ_INT((int)player_local->health, 110);
    ASSERT_EQ_INT((int)player_local->power, BZ_WOW_PLAYER_MAX_POWER);
    Wow_AIDie(creature, player);
    ASSERT_EQ_INT((int)player_local->xp, 50);
}

static void test_wow_projectile_kill_credits_player_caster(void) {
    LPEDICT player;
    LPEDICT creature;
    LPEDICT projectile = &wow_edicts[2];
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *projectile_local;

    test_prepare_pair(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    projectile_local = Wow_EntityLocal(projectile);
    player_local->kind = WOW_ENTITY_PLAYER;
    player_local->level = 1;
    creature_local->xp_reward = BZ_WOW_CREATURE_KILL_XP;
    projectile->inuse = true;
    projectile->s.number = 2;
    projectile_local->kind = WOW_ENTITY_PROJECTILE;
    projectile_local->projectile_caster = 0;
    Wow_AIDie(creature, projectile);
    ASSERT_EQ_INT((int)player_local->xp, BZ_WOW_CREATURE_KILL_XP);
}

static void test_wow_combat_generates_player_resource(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;

    test_prepare_pair(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    player_local->kind = WOW_ENTITY_PLAYER;
    player_local->max_power = BZ_WOW_PLAYER_MAX_POWER;
    Wow_DealDamage(creature, player, 1);
    ASSERT_EQ_INT((int)player_local->power, 5);
    ASSERT_EQ_INT((int)player->s.stats[ENT_MANA], 13);
    ASSERT_EQ_INT((int)creature_local->health, 2);
    ASSERT_EQ_INT((int)creature->s.stats[ENT_HEALTH], 170);
    player_local->power = 98;
    Wow_DealDamage(creature, player, 1);
    ASSERT_EQ_INT((int)player_local->power, BZ_WOW_PLAYER_MAX_POWER);
}

static void test_wow_creature_aggros_and_chases_player_by_proximity(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;
    WOWWORLDPROFILE profile;
    FLOAT before;

    test_prepare_player_creature(&player, &creature);
    CM_WowWorldProfileInit(true);
    creature->s.origin2.x = creature->s.origin.x = BZ_WOW_CREATURE_AGGRO_RANGE + 1.0f;
    local = Wow_EntityLocal(creature);
    local->home = creature->s.origin2;
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_IDLE);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_DISTANCE_REJECTS], 1);
    creature->s.origin2.x = creature->s.origin.x = 10.0f;
    local->home = creature->s.origin2;
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_AGGRO);
    ASSERT(local->enemy == player);
    before = creature->s.origin.x;
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_CHASE);
    ASSERT(creature->s.origin.x < before);
    CM_WowWorldProfileEnable(false);
}

static void test_wow_creature_aggros_from_player_damage_outside_proximity(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;

    test_prepare_player_creature(&player, &creature);
    creature->s.origin2.x = creature->s.origin.x = BZ_WOW_CREATURE_AGGRO_RANGE + 6.0f;
    local = Wow_EntityLocal(creature);
    local->home = creature->s.origin2;
    Wow_DealDamage(creature, player, 1);
    ASSERT(local->enemy == player);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_CHASE);
}

static void test_wow_creature_aggros_projectile_caster(void) {
    LPEDICT player;
    LPEDICT creature;
    LPEDICT projectile = &wow_edicts[2];
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *projectile_local;

    test_prepare_player_creature(&player, &creature);
    creature_local = Wow_EntityLocal(creature);
    projectile_local = Wow_EntityLocal(projectile);
    creature->s.origin2.x = creature->s.origin.x = BZ_WOW_CREATURE_AGGRO_RANGE + 6.0f;
    creature_local->home = creature->s.origin2;
    projectile->inuse = true;
    projectile_local->kind = WOW_ENTITY_PROJECTILE;
    projectile_local->projectile_caster = player->s.number;
    Wow_DealDamage(creature, projectile, 1);
    ASSERT(creature_local->enemy == player);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_CHASE);
}

static void test_wow_creature_attack_uses_cooldown_and_real_player_vitals(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;

    test_prepare_player_creature(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    creature->s.origin2.x = creature->s.origin.x = 4.0f;
    creature_local->home = creature->s.origin2;
    Wow_AIRunFrame(creature);
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_ATTACK);
    ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    Wow_AIRunFrame(creature);
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH - 1);
    ASSERT_EQ_INT((int)player_local->power, 3);
    ASSERT_EQ_INT((int)player->s.stats[ENT_HEALTH], 253);
    FOR_LOOP(i, 4) {
        Wow_AIRunFrame(creature);
        ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH - 1);
    }
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH - 2);
}

static void test_wow_creature_leashes_and_regenerates_without_xp(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;
    FLOAT before;

    test_prepare_player_creature(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    creature_local->health = 1;
    creature_local->home = (VECTOR2){ 0.0f, 0.0f };
    creature_local->enemy = player;
    creature_local->ai_state = WOW_AI_CHASE;
    creature->s.origin2.x = creature->s.origin.x = 20.0f;
    player->s.origin2.x = player->s.origin.x = BZ_WOW_CREATURE_LEASH_RANGE + 1.0f;
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_EVADE);
    ASSERT_NULL(creature_local->enemy);
    before = creature->s.origin.x;
    FOR_LOOP(i, 5) Wow_AIRunFrame(creature);
    ASSERT(creature->s.origin.x < before);
    ASSERT(creature_local->grounded);
    ASSERT_EQ_FLOAT(creature->s.origin.z, 7.0f, 0.001f);
    ASSERT_EQ_INT((int)creature_local->health, 2);
    ASSERT_EQ_INT((int)player_local->xp, 0);
}

static void test_wow_creature_chase_and_evade_follow_uneven_ground(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;

    test_prepare_player_creature(&player, &creature);
    local = Wow_EntityLocal(creature);
    g_ground_slope_x = 0.1f;
    player->s.origin.x = player->s.origin2.x = 10.0f;
    Wow_AIRunFrame(creature);
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_CHASE);
    ASSERT(creature->s.origin.x > 0.0f);
    ASSERT_EQ_FLOAT(creature->s.origin.z, 7.0f + creature->s.origin.x * 0.1f, 0.001f);
    ASSERT(local->grounded);

    player->s.origin.x = player->s.origin2.x = local->home.x + BZ_WOW_CREATURE_LEASH_RANGE + 1.0f;
    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_EVADE);
    FOR_LOOP(i, 3) Wow_AIRunFrame(creature);
    ASSERT_EQ_FLOAT(creature->s.origin.x, local->home.x, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.z, 7.0f + local->home.x * g_ground_slope_x, 0.001f);
    ASSERT(local->grounded);
}

static void test_wow_creature_retains_clear_local_steering(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;
    FLOAT first_y;
    SHORT steer_sign;
    WOWWORLDPROFILE profile;

    test_prepare_player_creature(&player, &creature);
    CM_WowWorldProfileInit(true);
    local = Wow_EntityLocal(creature);
    player->s.origin2 = (VECTOR2){ 10.0f, 0.0f };
    player->s.origin.x = 10.0f;
    creature->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
    creature->s.origin.x = 0.0f;
    local->home = creature->s.origin2;
    local->enemy = player;
    local->ai_state = WOW_AI_CHASE;
    Wow_AIResetNavigation(creature);
    g_world_mode = TEST_WORLD_WALL_X;

    Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->obstacle.state, WOW_AI_PATH_STEER);
    ASSERT_EQ_FLOAT(creature->s.origin.x, 0.0f, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.y, 0.0f, 0.001f);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_STEERING_PROBES], 4);
    steer_sign = local->obstacle.steer_sign;
    Wow_AIRunFrame(creature);
    first_y = creature->s.origin.y;
    ASSERT(fabsf(first_y) >= BZ_WOW_AI_PROGRESS_EPSILON);
    FOR_LOOP(i, 4) {
        Wow_AIRunFrame(creature);
        ASSERT_EQ_INT((int)local->obstacle.steer_sign, (int)steer_sign);
        ASSERT(first_y * creature->s.origin.y > 0.0f);
    }
    ASSERT(fabsf(creature->s.origin.y) > fabsf(first_y));
    ASSERT(local->obstacle.steer_time > 0);
    CM_WowWorldProfileEnable(false);
}

static void test_wow_blocked_creature_recovers_then_evades_without_teleport(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;
    VECTOR2 blocked_at, last_valid;
    WOWWORLDPROFILE profile;

    test_prepare_player_creature(&player, &creature);
    CM_WowWorldProfileInit(true);
    local = Wow_EntityLocal(creature);
    player->s.origin2 = (VECTOR2){ 20.0f, 0.0f };
    player->s.origin.x = 20.0f;
    creature->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
    creature->s.origin.x = 0.0f;
    local->home = creature->s.origin2;
    local->enemy = player;
    local->ai_state = WOW_AI_CHASE;
    Wow_AIResetNavigation(creature);
    FOR_LOOP(i, 5) Wow_AIRunFrame(creature);
    blocked_at = creature->s.origin2;
    last_valid = local->obstacle.last_valid;
    ASSERT(Wow_Distance2(&last_valid, &blocked_at) > 0.2f);

    g_world_mode = TEST_WORLD_BLOCKED;
    FOR_LOOP(i, BZ_WOW_AI_STUCK_TIME / FRAMETIME) Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->obstacle.state, WOW_AI_PATH_RECOVER);
    ASSERT_EQ_FLOAT(creature->s.origin.x, blocked_at.x, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.y, blocked_at.y, 0.001f);
    ASSERT(Wow_Distance2(&local->obstacle.last_valid, &creature->s.origin2) > 0.2f);

    FOR_LOOP(i, BZ_WOW_AI_STUCK_TIME * 3 / FRAMETIME + 2) Wow_AIRunFrame(creature);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_IDLE);
    ASSERT_NULL(local->enemy);
    ASSERT_EQ_FLOAT(creature->s.origin.x, blocked_at.x, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.y, blocked_at.y, 0.001f);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT(profile.counters[WOW_WORLD_PROFILE_AI_STEERING_PROBES] >= 4);
    ASSERT(profile.counters[WOW_WORLD_PROFILE_STUCK_RECOVERIES] >= 1);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_EVADE_FALLBACKS], 1);
    CM_WowWorldProfileEnable(false);
}

static void test_wow_creature_death_awards_once_and_respawns_clean(void) {
    LPEDICT player;
    LPEDICT creature;
    LPEDICT projectile = &wow_edicts[2];
    wowEntityLocal_t *player_local;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *projectile_local;

    test_prepare_player_creature(&player, &creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    projectile_local = Wow_EntityLocal(projectile);
    creature_local->health = 1;
    creature_local->home = (VECTOR2){ 12.0f, 8.0f };
    creature_local->enemy = player;
    player_local->enemy = creature;
    player_local->attack_damage_time = 100;
    player->client->ps.selected_entity = creature->s.number;
    creature->selected = 1;
    projectile->inuse = true;
    projectile_local->kind = WOW_ENTITY_PROJECTILE;
    projectile_local->projectile_target = creature->s.number;
    Wow_DealDamage(creature, player, 1);
    ASSERT(creature_local->dead);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_DEAD);
    ASSERT_EQ_INT((int)creature_local->health, 0);
    ASSERT((creature->svflags & SVF_DEADMONSTER) != 0);
    ASSERT((creature->svflags & SVF_MONSTER) == 0);
    ASSERT_NULL(player_local->enemy);
    ASSERT_EQ_INT((int)player->client->ps.selected_entity, 0);
    ASSERT_EQ_INT((int)creature->selected, 0);
    ASSERT(!projectile->inuse);
    ASSERT_EQ_INT((int)player_local->xp, BZ_WOW_CREATURE_KILL_XP);
    ASSERT_EQ_INT((int)creature_local->loot_state, WOW_LOOT_AVAILABLE);
    ASSERT_EQ_INT((int)creature_local->num_loot, BZ_WOW_MAX_LOOT_ITEMS);
    Wow_AIDie(creature, player);
    ASSERT_EQ_INT((int)player_local->xp, BZ_WOW_CREATURE_KILL_XP);
    ASSERT_EQ_INT((int)creature_local->num_loot, BZ_WOW_MAX_LOOT_ITEMS);
    player->s.origin2.x = player->s.origin.x = 100.0f;
    creature_local->obstacle.state = WOW_AI_PATH_FAILED;
    creature_local->obstacle.stuck_time = 999;
    creature_local->obstacle.steer_time = 777;
    creature_local->obstacle.recoveries = 1;
    FOR_LOOP(i, 70) Wow_AIRunFrame(creature);
    ASSERT(!creature_local->dead);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_IDLE);
    ASSERT_EQ_INT((int)creature_local->health, (int)creature_local->max_health);
    ASSERT_NULL(creature_local->enemy);
    ASSERT((creature->svflags & SVF_MONSTER) != 0);
    ASSERT((creature->svflags & SVF_DEADMONSTER) == 0);
    ASSERT_EQ_FLOAT(creature->s.origin.x, creature_local->home.x, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.y, creature_local->home.y, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.z, 7.0f, 0.001f);
    ASSERT(creature_local->grounded);
    ASSERT_EQ_INT((int)creature_local->ground_surface, WOW_SURFACE_TERRAIN);
    ASSERT_EQ_INT((int)creature->s.stats[ENT_HEALTH], 255);
    ASSERT_EQ_INT((int)creature_local->loot_state, WOW_LOOT_NONE);
    ASSERT_EQ_INT((int)creature_local->num_loot, 0);
    ASSERT_EQ_INT((int)creature_local->obstacle.state, WOW_AI_PATH_DIRECT);
    ASSERT_EQ_INT((int)creature_local->obstacle.stuck_time, 0);
    ASSERT_EQ_INT((int)creature_local->obstacle.steer_time, 0);
    ASSERT_EQ_INT((int)creature_local->obstacle.recoveries, 0);
    ASSERT_EQ_FLOAT(creature_local->obstacle.last_valid.x, creature_local->home.x, 0.001f);
    ASSERT_EQ_FLOAT(creature_local->obstacle.last_valid.y, creature_local->home.y, 0.001f);
}

static void test_wow_creature_respawn_defers_without_ground(void) {
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *local;
    VECTOR3 before;

    test_prepare_player_creature(&player, &creature);
    (void)player;
    local = Wow_EntityLocal(creature);
    local->dead = true;
    local->ai_state = WOW_AI_RESPAWN;
    local->respawn_time = 0;
    local->home = (VECTOR2){ 12.0f, 8.0f };
    local->home_z = 7.0f;
    before = creature->s.origin;
    g_ground_enabled = false;
    Wow_AIRunFrame(creature);
    ASSERT(local->dead);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_RESPAWN);
    ASSERT_EQ_INT((int)local->respawn_time, BZ_WOW_CREATURE_RESPAWN_TIME);
    ASSERT_EQ_FLOAT(creature->s.origin.x, before.x, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.origin.z, before.z, 0.001f);
}

/* Production scheduling wakes combat immediately while dormant and distant idle states avoid world queries. */
static void test_wow_creature_scheduler_budgets_state_rates(void) {
    LPEDICT player, creature;
    wowEntityLocal_t *local;
    WOWWORLDPROFILE profile;
    VECTOR2 before;

    test_prepare_player_creature(&player, &creature);
    local = Wow_EntityLocal(creature);
    player->s.origin.x = player->s.origin2.x = 100.0f;
    local->patrol_radius = local->walk_speed = 0.0f;
    local->update.rate = WOW_AI_UPDATE_COUNT;
    CM_WowWorldProfileInit(true);
    FOR_LOOP(i, 20) Wow_RunCreatureFrame(creature);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_DORMANT_FRAMES], 20);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_DEFERRED_UPDATES], 20);
    ASSERT_EQ_INT((int)g_ground_query_calls, 0);
    ASSERT_EQ_INT((int)g_world_sweep_calls, 0);

    local->patrol_radius = local->walk_speed = 2.0f;
    before = creature->s.origin2;
    FOR_LOOP(i, BZ_WOW_AI_LOW_UPDATE / FRAMETIME) Wow_RunCreatureFrame(creature);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_LOW_UPDATES], 1);
    ASSERT(g_ground_query_calls > 0);
    ASSERT(g_world_sweep_calls > 0);
    ASSERT(Wow_Distance2(&before, &creature->s.origin2) > 0.1f);

    player->s.origin.x = player->s.origin2.x = 20.0f;
    FOR_LOOP(i, BZ_WOW_AI_MEDIUM_UPDATE / FRAMETIME) Wow_RunCreatureFrame(creature);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_MEDIUM_UPDATES], 1);
    player->s.origin.x = player->s.origin2.x = 10.0f;
    Wow_RunCreatureFrame(creature);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_HIGH_UPDATES], 1);
    ASSERT_EQ_INT((int)local->ai_state, WOW_AI_AGGRO);
    ASSERT(local->enemy == player);
    CM_WowWorldProfileEnable(false);
}

/* High-rate combat keeps the existing millisecond damage point under the scheduler boundary. */
static void test_wow_budgeted_attack_timing_remains_server_time(void) {
    LPEDICT player, creature;
    wowEntityLocal_t *player_local, *local;
    WOWWORLDPROFILE profile;

    test_prepare_player_creature(&player, &creature);
    player_local = Wow_EntityLocal(player);
    local = Wow_EntityLocal(creature);
    creature->s.origin.x = creature->s.origin2.x = 4.0f;
    local->home = creature->s.origin2;
    local->enemy = player;
    local->ai_state = WOW_AI_ATTACK;
    local->update.rate = WOW_AI_UPDATE_COUNT;
    creature->attack(creature);
    CM_WowWorldProfileInit(true);
    Wow_RunCreatureFrame(creature);
    ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    Wow_RunCreatureFrame(creature);
    ASSERT_EQ_INT((int)player_local->health, BZ_WOW_PLAYER_BASE_HEALTH - 1);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_HIGH_UPDATES], 2);
    CM_WowWorldProfileEnable(false);
}

/* Dead lifecycle work advances on accumulated server time and performs no sweep before the respawn transition. */
static void test_wow_budgeted_respawn_remains_time_based_without_dead_sweeps(void) {
    LPEDICT player, creature;
    wowEntityLocal_t *local;
    WOWWORLDPROFILE profile;

    test_prepare_player_creature(&player, &creature);
    (void)player;
    local = Wow_EntityLocal(creature);
    local->dead = true;
    local->health = 0;
    local->ai_state = WOW_AI_RESPAWN;
    local->death_time = 0;
    local->respawn_time = BZ_WOW_CREATURE_RESPAWN_TIME;
    local->update.rate = WOW_AI_UPDATE_COUNT;
    CM_WowWorldProfileInit(true);
    FOR_LOOP(i, BZ_WOW_CREATURE_RESPAWN_TIME / FRAMETIME - 1) Wow_RunCreatureFrame(creature);
    ASSERT(local->dead);
    ASSERT_EQ_INT((int)g_ground_query_calls, 0);
    ASSERT_EQ_INT((int)g_world_sweep_calls, 0);
    Wow_RunCreatureFrame(creature);
    ASSERT(!local->dead);
    ASSERT(g_ground_query_calls > 0);
    CM_WowWorldProfileSnapshot(&profile);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_LOW_UPDATES],
                  BZ_WOW_CREATURE_RESPAWN_TIME / BZ_WOW_AI_DEAD_UPDATE);
    ASSERT_EQ_INT((int)profile.counters[WOW_WORLD_PROFILE_AI_DEFERRED_UPDATES],
                  BZ_WOW_CREATURE_RESPAWN_TIME / FRAMETIME -
                      BZ_WOW_CREATURE_RESPAWN_TIME / BZ_WOW_AI_DEAD_UPDATE);
    CM_WowWorldProfileEnable(false);
}

int main(void) {
    RUN_TEST(test_wow_attack_applies_damage_after_damage_point);
    RUN_TEST(test_wow_attack_uses_explicit_timing_over_animation_split);
    RUN_TEST(test_wow_swing_requires_target_in_range_at_damage_point);
    RUN_TEST(test_wow_walls_block_proximity_aggro_and_melee_start);
    RUN_TEST(test_wow_wall_at_damage_point_cancels_melee_hit);
    RUN_TEST(test_wow_attack_lethal_triggers_death_state);
    RUN_TEST(test_wow_dead_entity_ignores_pain_and_attack);
    RUN_TEST(test_wow_death_holds_terminal_frame);
    RUN_TEST(test_wow_vitals_publish_living_health_fraction);
    RUN_TEST(test_wow_player_kill_grants_xp_and_levels_with_overflow);
    RUN_TEST(test_wow_projectile_kill_credits_player_caster);
    RUN_TEST(test_wow_combat_generates_player_resource);
    RUN_TEST(test_wow_creature_aggros_and_chases_player_by_proximity);
    RUN_TEST(test_wow_creature_aggros_from_player_damage_outside_proximity);
    RUN_TEST(test_wow_creature_aggros_projectile_caster);
    RUN_TEST(test_wow_creature_attack_uses_cooldown_and_real_player_vitals);
    RUN_TEST(test_wow_creature_leashes_and_regenerates_without_xp);
    RUN_TEST(test_wow_creature_chase_and_evade_follow_uneven_ground);
    RUN_TEST(test_wow_creature_retains_clear_local_steering);
    RUN_TEST(test_wow_blocked_creature_recovers_then_evades_without_teleport);
    RUN_TEST(test_wow_creature_death_awards_once_and_respawns_clean);
    RUN_TEST(test_wow_creature_respawn_defers_without_ground);
    RUN_TEST(test_wow_creature_scheduler_budgets_state_rates);
    RUN_TEST(test_wow_budgeted_attack_timing_remains_server_time);
    RUN_TEST(test_wow_budgeted_respawn_remains_time_based_without_dead_sweeps);
    TEST_RESULTS();
}
