#include "g_wow_local.h"
#include <math.h>

static wowMove_t wow_move_stand = { "Stand", NULL, NULL };
static wowMove_t wow_move_ready = { "Ready", NULL, NULL };
static wowMove_t wow_move_walk = { "Walk", NULL, NULL };
static wowMove_t wow_move_run = { "Run", NULL, NULL };
static wowMove_t wow_move_attack = { "Attack", NULL, NULL };
static wowMove_t wow_move_pain = { "Pain", NULL, NULL };
static wowMove_t wow_move_death = { "Death", NULL, NULL };

#define WOW_DEFAULT_ATTACK_DAMAGE_POINT 250
#define WOW_DEFAULT_ATTACK_BACKSWING 450
#define WOW_DEFAULT_PAIN_TIME 450
#define WOW_DEFAULT_DEATH_TIME 1200

/* Projectile damage belongs to its caster for retaliation, power gain, XP, and quest credit. */
LPEDICT Wow_CombatOwner(LPEDICT attacker) {
    wowEntityLocal_t *local = Wow_EntityLocal(attacker);
    DWORD number;

    if (!attacker || !local || local->kind != WOW_ENTITY_PROJECTILE) return attacker;
    number = local->projectile_caster;
    if (number >= WOW_MAX_EDICTS || !wow_edicts[number].inuse) return NULL;
    return &wow_edicts[number];
}

/* Network entity bars use one byte; keep every living entity visible even below one percent. */
void Wow_SyncEntityVitals(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local) return;
    ent->s.stats[ENT_HEALTH] = !local->dead && local->health && local->max_health
        ? (BYTE)MAX(1, MIN(255, (local->health * 255 + local->max_health - 1) / local->max_health)) : 0;
    ent->s.stats[ENT_MANA] = local->power && local->max_power
        ? (BYTE)MAX(1, MIN(255, (local->power * 255 + local->max_power - 1) / local->max_power)) : 0;
}

/* Selection includes neutral actors, while combat targeting admits only players and hostile living creatures. */
BOOL Wow_EntityCanBeSelected(LPCEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    return ent && ent->inuse && local && (local->kind == WOW_ENTITY_PLAYER || local->kind == WOW_ENTITY_CREATURE) &&
        !local->dead && local->health > 0;
}

BOOL Wow_EntityCanBeTargeted(LPCEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    return Wow_EntityCanBeSelected(ent) &&
        (local->kind == WOW_ENTITY_PLAYER || (local->kind == WOW_ENTITY_CREATURE && local->hostile));
}

/* TODO: Replace this compact scaffold curve with authoritative server progression data. */
DWORD Wow_XpForNextLevel(DWORD level) {
    if (!level || level >= BZ_WOW_MAX_LEVEL) return 0;
    return 400 + (level - 1) * 500;
}

/* Every XP source uses one overflow-preserving level transition and one authoritative player state. */
void Wow_AwardXp(LPEDICT player, DWORD amount) {
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    DWORD needed, old_level;

    if (!player || !local || local->kind != WOW_ENTITY_PLAYER || !amount) return;
    old_level = local->level;
    local->xp += amount;
    while ((needed = Wow_XpForNextLevel(local->level)) && local->xp >= needed) {
        local->xp -= needed;
        local->level++;
        local->max_health = BZ_WOW_PLAYER_BASE_HEALTH +
            (local->level - 1) * BZ_WOW_PLAYER_HEALTH_PER_LEVEL;
        local->health = local->max_health;
        local->power = local->max_power;
        fprintf(stderr, "OpenWoW level: reached level %u\n", (unsigned)local->level);
    }
    if (local->level >= BZ_WOW_MAX_LEVEL) local->xp = 0;
    Wow_SyncEntityVitals(player);
    Wow_SetCombatMessage(player, local->level != old_level ? WOW_COMBAT_MESSAGE_LEVEL : WOW_COMBAT_MESSAGE_XP,
                         local->level != old_level ? local->level : amount);
}

/* A kill is awarded once by Wow_AIDie after combat ownership resolves through the projectile caster. */
void Wow_AwardKillXp(LPEDICT attacker, LPEDICT victim) {
    LPEDICT source = Wow_CombatOwner(attacker);
    wowEntityLocal_t *source_local = Wow_EntityLocal(source);
    wowEntityLocal_t *victim_local = Wow_EntityLocal(victim);

    if (!source || !victim || source == victim || !source_local || !victim_local ||
        source_local->kind != WOW_ENTITY_PLAYER || victim_local->kind != WOW_ENTITY_CREATURE ||
        !victim_local->xp_reward) return;
    Wow_AwardXp(source, victim_local->xp_reward);
}

static void Wow_GainCombatPower(LPEDICT ent, DWORD amount) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || local->kind != WOW_ENTITY_PLAYER || !local->max_power) return;
    local->power = MIN(local->power + amount, local->max_power);
    Wow_SyncEntityVitals(ent);
}

FLOAT Wow_Distance2(LPCVECTOR2 a, LPCVECTOR2 b) {
    VECTOR2 delta = Vector2_sub(a, b);
    return Vector2_len(&delta);
}

static void Wow_ResetAttack(wowEntityLocal_t *local) {
    if (!local) return;
    local->attack_damage = local->kind == WOW_ENTITY_CREATURE
        ? BZ_WOW_CREATURE_ATTACK_DAMAGE : BZ_WOW_STRIKE_DAMAGE;
    local->attack_time = 0;
    local->attack_damage_time = 0;
    local->attack_backswing_time = 0;
    local->attack_damage_done = false;
    local->pain_time = 0;
}

/* Death invalidates every stale target and projectile without reallocating the creature entity. */
static void Wow_ClearCombatReferences(LPEDICT victim) {
    DWORD victim_number;

    if (!victim) return;
    victim_number = victim->s.number;
    victim->selected = 0;
    FOR_LOOP(i, WOW_MAX_EDICTS) {
        LPEDICT ent = &wow_edicts[i];
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (!ent->inuse || !local) continue;
        if (local->enemy == victim) {
            local->enemy = NULL;
            Wow_ResetAttack(local);
        }
        if (local->kind == WOW_ENTITY_PROJECTILE && local->projectile_target == victim_number)
            ent->inuse = false;
        if (ent->client && ent->client->ps.selected_entity == victim_number)
            ent->client->ps.selected_entity = 0;
    }
}

static void Wow_FaceTarget(LPEDICT ent, LPEDICT target) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    VECTOR2 delta;

    if (!ent || !target || !local) {
        return;
    }

    delta = Vector2_sub(&target->s.origin2, &ent->s.origin2);
    if (Vector2_len(&delta) <= 0.001f) {
        return;
    }
    local->yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
    ent->s.angle = (FLOAT)DEG2RAD(local->yaw);
}

static void Wow_AttackTimingFromAnimation(wowEntityLocal_t const *local,
                                          DWORD *damage_point,
                                          DWORD *backswing) {
    DWORD dp = WOW_DEFAULT_ATTACK_DAMAGE_POINT;
    DWORD bs = WOW_DEFAULT_ATTACK_BACKSWING;

    if (local) {
        if (local->attack_damage_point > 0) {
            dp = local->attack_damage_point;
        }
        if (local->attack_backswing > 0) {
            bs = local->attack_backswing;
        }
        if (local->animation && local->attack_damage_point == 0 && local->attack_backswing == 0) {
            DWORD duration = local->animation->interval[1] > local->animation->interval[0]
                ? local->animation->interval[1] - local->animation->interval[0]
                : 0;
            if (duration > 0) {
                /* Prefer M2 event-derived damage_point over the 35% fallback */
                if (local->animation->damage_point > 0) {
                    dp = MIN(local->animation->damage_point, duration);
                } else {
                    dp = MAX(1, duration * 35 / 100);
                }
                bs = MAX(1, duration - dp);
            }
        }
    }

    if (damage_point) {
        *damage_point = dp;
    }
    if (backswing) {
        *backswing = bs;
    }
}

static void Wow_AdvanceDeathFrame(LPEDICT ent, wowEntityLocal_t *local) {
    DWORD end_frame;

    if (!ent || !local || !local->animation) {
        return;
    }

    end_frame = local->animation->interval[1] > local->animation->interval[0]
        ? local->animation->interval[1] - 1
        : local->animation->interval[0];

    if (ent->s.frame < end_frame) {
        DWORD next_frame = ent->s.frame + FRAMETIME;
        ent->s.frame = MIN(next_frame, end_frame);
    } else {
        ent->s.frame = end_frame;
    }
}

void Wow_DealDamage(LPEDICT target, LPEDICT attacker, DWORD damage) {
    wowEntityLocal_t *target_local;
    LPEDICT source = Wow_CombatOwner(attacker);

    if (!target || damage == 0) {
        return;
    }

    target_local = Wow_EntityLocal(target);
    if (!target_local || target_local->dead || target_local->health == 0 ||
        (target_local->kind == WOW_ENTITY_CREATURE && !target_local->hostile)) {
        return;
    }

    damage = Wow_AdjustIncomingDamage(target, damage);
    Wow_GainCombatPower(source, damage * 5);
    Wow_GainCombatPower(target, damage * 3);
    Wow_SetCombatMessage(source, WOW_COMBAT_MESSAGE_DAMAGE_DEALT, MIN(damage, target_local->health));
    Wow_SetCombatMessage(target, WOW_COMBAT_MESSAGE_DAMAGE_TAKEN, MIN(damage, target_local->health));
    if (target_local->health <= damage) {
        Wow_AIDie(target, attacker);
        return;
    }

    target_local->health -= damage;
    Wow_SyncEntityVitals(target);
    if (target_local->kind == WOW_ENTITY_CREATURE && source && source != target) {
        target_local->enemy = source;
        target_local->ai_state = WOW_AI_AGGRO;
    }

    /* Quake2-style reaction: retaliate when not already locked, else play pain. */
    if (source && source != target && target->attack &&
        target_local->attack_damage_time == 0 &&
        target_local->attack_backswing_time == 0 &&
        target_local->pain_time == 0 &&
        target_local->death_time == 0) {
        target_local->enemy = source;
        target->attack(target);
        return;
    }

    if (target->pain) {
        target->pain(target);
    }
}

BOOL Wow_EntityAffectingCombat(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    wowEntityLocal_t *target_local;
    LPEDICT target;

    if (!ent || !local) {
        return false;
    }
    target = local->enemy;
    if (!target || target == ent || !Wow_EntityCanBeTargeted(target)) {
        local->enemy = NULL;
        return false;
    }
    target_local = Wow_EntityLocal(target);
    if (local->dead || local->health == 0 || !target_local) {
        local->enemy = NULL;
        return false;
    }
    return true;
}

BOOL Wow_SetStandMove(LPEDICT ent) {
    return Wow_SetEntityMove(ent, &wow_move_stand);
}

BOOL Wow_SetRunMove(LPEDICT ent) {
    return Wow_SetEntityMove(ent, &wow_move_run);
}

BOOL Wow_SetWalkMove(LPEDICT ent) {
    return Wow_SetEntityMove(ent, &wow_move_walk);
}

BOOL Wow_SetCombatReadyAnimation(LPEDICT ent) {
    static LPCSTR const weapon_ready_animations[] = {
        "Ready1H",
        "ReadyUnarmed",
        "Ready2H",
        "Ready2HL",
        NULL,
    };
    static LPCSTR const unarmed_ready_animations[] = {
        "ReadyUnarmed",
        "Ready1H",
        "Ready2H",
        "Ready2HL",
        NULL,
    };

    if (Wow_SetEntityMoveFirstAnimation(ent,
        &wow_move_ready,
        ent && ent->s.model2 ? weapon_ready_animations : unarmed_ready_animations)) {
        return true;
    }
    return Wow_SetStandMove(ent);
}

void Wow_AIIdle(LPEDICT ent) {
    if (Wow_EntityAffectingCombat(ent)) {
        Wow_SetCombatReadyAnimation(ent);
    } else {
        Wow_SetStandMove(ent);
    }
}

void Wow_AIMove(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    VECTOR2 target;
    VECTOR2 delta;
    FLOAT len;
    FLOAT step;
    VECTOR2 displacement;

    if (!ent || !local) {
        return;
    }

    target = (VECTOR2){
        local->home.x + cosf(local->patrol_phase) * local->patrol_radius,
        local->home.y + sinf(local->patrol_phase) * local->patrol_radius,
    };
    delta = Vector2_sub(&target, &ent->s.origin2);
    len = Vector2_len(&delta);
    if (len <= 0.001f) {
        return;
    }

    step = MIN(local->walk_speed * ((FLOAT)FRAMETIME / 1000.0f), len);
    displacement = (VECTOR2){ delta.x * step / len, delta.y * step / len };
    if (!Wow_MoveEntity(ent, &displacement, (FLOAT)FRAMETIME / 1000.0f)) {
        Wow_SetStandMove(ent);
        return;
    }
    local->yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
    ent->s.angle = (FLOAT)DEG2RAD(local->yaw);
    Wow_SetWalkMove(ent);
}

void Wow_AIAttack(LPEDICT ent) {
    static LPCSTR const attack_animations[] = {
        "Attack1H",
        "AttackUnarmed",
        "Attack2H",
        "Attack",
        NULL,
    };
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPEDICT target;

    DWORD damage_point;
    DWORD backswing;

    if (!ent || !local || local->dead) {
        return;
    }

    if (local->attack_damage_time > 0 || local->attack_backswing_time > 0 ||
        local->pain_time > 0 || local->death_time > 0) {
        return;
    }

    target = Wow_EntityAffectingCombat(ent) ? local->enemy : NULL;
    if (!target) {
        return;
    }

    Wow_FaceTarget(ent, target);

    /* Don't start the swing animation if out of melee range — the per-frame
     * chase logic in Wow_RunFrame will close the gap automatically. */
    {
        VECTOR2 delta = Vector2_sub(&target->s.origin2, &ent->s.origin2);
        if (Vector2_len(&delta) > WOW_MELEE_RANGE) {
            if (local->kind == WOW_ENTITY_CREATURE) local->ai_state = WOW_AI_CHASE;
            return;
        }
    }

    if (!Wow_SetEntityMoveFirstAnimation(ent, &wow_move_attack, attack_animations)) {
        return;
    }

    Wow_AttackTimingFromAnimation(local, &damage_point, &backswing);
    local->attack_damage_time = damage_point;
    local->attack_backswing_time = backswing;
    local->attack_time = damage_point + backswing;
    local->attack_damage_done = false;
    if (local->kind == WOW_ENTITY_CREATURE) local->ai_state = WOW_AI_ATTACK;
}

void Wow_AIPain(LPEDICT ent) {
    static LPCSTR const pain_animations[] = {
        "CombatWound",
        "StandWound",
        "Stun",
        NULL,
    };
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || local->dead || local->health == 0) {
        return;
    }

    local->pain_time = WOW_DEFAULT_PAIN_TIME;
    Wow_SetEntityMoveFirstAnimation(ent, &wow_move_pain, pain_animations);
}

void Wow_AIDie(LPEDICT ent, LPEDICT attacker) {
    static LPCSTR const death_animations[] = {
        "Death",
        "Dead",
        NULL,
    };
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPEDICT source = Wow_CombatOwner(attacker);

    if (!ent || !local || local->dead) {
        return;
    }

    local->dead = true;
    local->ai_state = WOW_AI_DEAD;
    local->health = 0;
    local->enemy = NULL;
    Wow_ResetAttack(local);
    local->respawn_time = local->kind == WOW_ENTITY_CREATURE ? BZ_WOW_CREATURE_RESPAWN_TIME : 0;
    local->regen_time = 0;

    ent->svflags |= SVF_DEADMONSTER;
    if (local->kind == WOW_ENTITY_CREATURE) ent->svflags &= ~SVF_MONSTER;
    Wow_ClearCombatReferences(ent);
    Wow_SyncEntityVitals(ent);
    Wow_AwardKillXp(source, ent);
    if (local->kind == WOW_ENTITY_CREATURE) Wow_GenerateLoot(ent);
    Wow_QuestCreatureKilled(source, ent);
    if (Wow_SetEntityMoveFirstAnimation(ent, &wow_move_death, death_animations) && local->animation) {
        local->death_time = MAX(1, local->animation->interval[1] - local->animation->interval[0]);
    } else {
        local->death_time = WOW_DEFAULT_DEATH_TIME;
    }
}

BOOL Wow_AIAdvanceLockedFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    BOOL finished;

    if (!ent || !local) {
        return false;
    }

    if (local->dead) {
        if (local->death_time > 0) {
            finished = local->death_time <= FRAMETIME;
            if (local->death_time > FRAMETIME) {
                local->death_time -= FRAMETIME;
            } else {
                local->death_time = 0;
            }
            Wow_AdvanceDeathFrame(ent, local);
            if (finished) {
                local->death_time = 0;
                Wow_AdvanceDeathFrame(ent, local);
            }
        } else {
            Wow_AdvanceDeathFrame(ent, local);
        }
        return true;
    }

    if (local->attack_damage_time > 0) {
        finished = local->attack_damage_time <= FRAMETIME;
        if (local->attack_damage_time > FRAMETIME) {
            local->attack_damage_time -= FRAMETIME;
        } else {
            local->attack_damage_time = 0;
        }
        local->attack_time = local->attack_damage_time + local->attack_backswing_time;
        Wow_AdvanceEntityFrame(ent);
        if (finished && !local->attack_damage_done) {
            LPEDICT target = Wow_EntityAffectingCombat(ent) ? local->enemy : NULL;
            DWORD damage = local->attack_damage ? local->attack_damage :
                (local->kind == WOW_ENTITY_CREATURE ? BZ_WOW_CREATURE_ATTACK_DAMAGE : BZ_WOW_STRIKE_DAMAGE);

            local->attack_damage_done = true;
            /* Enhanced strikes replace exactly one shared damage point, then auto-attacks return to base damage. */
            local->attack_damage = BZ_WOW_STRIKE_DAMAGE;
            if (target && Wow_Distance2(&target->s.origin2, &ent->s.origin2) <= WOW_MELEE_RANGE)
                Wow_DealDamage(target, ent, damage);
        }
        return true;
    }

    if (local->attack_backswing_time > 0) {
        finished = local->attack_backswing_time <= FRAMETIME;
        if (local->attack_backswing_time > FRAMETIME) {
            local->attack_backswing_time -= FRAMETIME;
        } else {
            local->attack_backswing_time = 0;
        }
        local->attack_time = local->attack_backswing_time;
        Wow_AdvanceEntityFrame(ent);
        if (finished) {
            local->attack_time = 0;
            local->attack_damage_done = false;
            if (Wow_EntityAffectingCombat(ent)) {
                Wow_SetCombatReadyAnimation(ent);
                ent->attack(ent); /* auto-reattack after each swing */
            } else {
                Wow_SetStandMove(ent);
            }
        }
        return true;
    }

    if (local->pain_time > 0) {
        finished = local->pain_time <= FRAMETIME;
        if (local->pain_time > FRAMETIME) {
            local->pain_time -= FRAMETIME;
        } else {
            local->pain_time = 0;
        }
        Wow_AdvanceEntityFrame(ent);
        if (finished) {
            if (Wow_EntityAffectingCombat(ent)) {
                Wow_SetCombatReadyAnimation(ent);
            } else {
                Wow_SetStandMove(ent);
            }
        }
        return true;
    }

    return false;
}

/* Chase and evade share movement but use different destinations, speeds, and stopping distances. */
static BOOL Wow_AIMoveToward(LPEDICT ent, LPCVECTOR2 target, FLOAT speed, FLOAT stop_distance) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    VECTOR2 delta;
    FLOAT distance;
    FLOAT step;
    VECTOR2 displacement;

    if (!ent || !local || !target) return false;
    delta = Vector2_sub(target, &ent->s.origin2);
    distance = Vector2_len(&delta);
    if (distance <= stop_distance || distance <= 0.001f) return false;
    step = MIN(speed * ((FLOAT)FRAMETIME / 1000.0f), distance - stop_distance);
    displacement = (VECTOR2){ delta.x * step / distance, delta.y * step / distance };
    if (!Wow_MoveEntity(ent, &displacement, (FLOAT)FRAMETIME / 1000.0f)) return true;
    local->yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
    ent->s.angle = (FLOAT)DEG2RAD(local->yaw);
    Wow_SetRunMove(ent);
    return true;
}

/* Idle creatures only acquire the single server player inside their configured aggro radius. */
static BOOL Wow_AIAcquirePlayer(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPEDICT player = &wow_edicts[0];

    if (!ent || !local || !local->hostile || !Wow_EntityCanBeTargeted(player) ||
        Wow_Distance2(&player->s.origin2, &ent->s.origin2) > BZ_WOW_CREATURE_AGGRO_RANGE) return false;
    local->enemy = player;
    local->ai_state = WOW_AI_AGGRO;
    Wow_SetCombatReadyAnimation(ent);
    return true;
}

static BOOL Wow_AIShouldEvade(LPEDICT ent, wowEntityLocal_t *local) {
    return !ent || !local || !local->enemy ||
        Wow_Distance2(&ent->s.origin2, &local->home) > BZ_WOW_CREATURE_LEASH_RANGE ||
        Wow_Distance2(&local->enemy->s.origin2, &local->home) > BZ_WOW_CREATURE_LEASH_RANGE;
}

/* Evade clears combat ownership immediately and restores health on a fixed server-time cadence. */
static void Wow_AIEnterEvade(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local) return;
    local->enemy = NULL;
    local->ai_state = WOW_AI_EVADE;
    local->regen_time = BZ_WOW_CREATURE_REGEN_TIME;
    Wow_ResetAttack(local);
    Wow_SetRunMove(ent);
}

static void Wow_AIRegenerate(LPEDICT ent, wowEntityLocal_t *local) {
    if (!ent || !local || local->health >= local->max_health) return;
    if (local->regen_time > FRAMETIME) {
        local->regen_time -= FRAMETIME;
        return;
    }
    local->regen_time = BZ_WOW_CREATURE_REGEN_TIME;
    local->health++;
    Wow_SyncEntityVitals(ent);
}

/* Respawn reuses the same edict while restoring every combat-owned field to its spawn contract. */
static void Wow_AIRespawn(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    VECTOR3 home;

    if (!ent || !local || local->kind != WOW_ENTITY_CREATURE) return;
    home = (VECTOR3){ local->home.x, local->home.y, local->home_z };
    if (!Wow_PlaceEntityOnGround(ent, &home)) {
        local->respawn_time = BZ_WOW_CREATURE_RESPAWN_TIME;
        fprintf(stderr, "OpenWoW physics: creature %u respawn deferred without ground at %.3f %.3f\n",
                (unsigned)ent->s.number, (double)home.x, (double)home.y);
        return;
    }
    local->dead = false;
    local->ai_state = WOW_AI_IDLE;
    local->health = local->max_health;
    local->enemy = NULL;
    local->death_time = local->respawn_time = local->regen_time = 0;
    Wow_ResetAttack(local);
    Wow_ClearLoot(ent);
    ent->svflags = (ent->svflags | SVF_MONSTER) & ~SVF_DEADMONSTER;
    ent->selected = 0;
    Wow_SyncEntityVitals(ent);
    Wow_SetStandMove(ent);
}

void Wow_AIRunFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    FLOAT target_distance;

    if (!ent || !local) {
        return;
    }

    if (local->dead) {
        (void)Wow_AIAdvanceLockedFrame(ent);
        if (local->kind != WOW_ENTITY_CREATURE) return;
        if (local->ai_state == WOW_AI_DEAD && !local->death_time) local->ai_state = WOW_AI_RESPAWN;
        if (local->ai_state == WOW_AI_RESPAWN) {
            if (local->respawn_time > FRAMETIME) local->respawn_time -= FRAMETIME;
            else Wow_AIRespawn(ent);
        }
        return;
    }

    if (local->ai_state == WOW_AI_EVADE) {
        Wow_AIRegenerate(ent, local);
        if (!Wow_AIMoveToward(ent, &local->home, BZ_WOW_CREATURE_EVADE_SPEED, 0.0f) &&
            local->health == local->max_health) {
            local->ai_state = WOW_AI_IDLE;
            Wow_SetStandMove(ent);
        }
        Wow_AdvanceEntityFrame(ent);
        return;
    }

    if (local->ai_state == WOW_AI_IDLE) {
        if (Wow_AIAcquirePlayer(ent)) {
            Wow_AdvanceEntityFrame(ent);
            return;
        }
        if (local->patrol_radius <= 0.0f || local->walk_speed <= 0.0f) {
            (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, (FLOAT)FRAMETIME / 1000.0f);
            if (ent->idle) ent->idle(ent);
            Wow_AdvanceEntityFrame(ent);
            return;
        }
        local->patrol_phase += ((FLOAT)FRAMETIME / 1000.0f) * 0.6f;
        if (ent->move) ent->move(ent);
        Wow_AdvanceEntityFrame(ent);
        return;
    }

    if (!Wow_EntityAffectingCombat(ent) || Wow_AIShouldEvade(ent, local)) {
        Wow_AIEnterEvade(ent);
        return;
    }
    if (Wow_AIAdvanceLockedFrame(ent)) {
        (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, (FLOAT)FRAMETIME / 1000.0f);
        return;
    }
    target_distance = Wow_Distance2(&local->enemy->s.origin2, &ent->s.origin2);
    if (target_distance > WOW_MELEE_RANGE) {
        local->ai_state = WOW_AI_CHASE;
        Wow_AIMoveToward(ent, &local->enemy->s.origin2, BZ_WOW_CREATURE_CHASE_SPEED, WOW_MELEE_RANGE);
    } else {
        local->ai_state = WOW_AI_ATTACK;
        (void)Wow_MoveEntity(ent, &(VECTOR2){ 0.0f, 0.0f }, (FLOAT)FRAMETIME / 1000.0f);
        if (ent->attack) ent->attack(ent);
    }
    Wow_AdvanceEntityFrame(ent);
}

void Wow_RunCreatureFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || local->kind != WOW_ENTITY_CREATURE) {
        return;
    }
    if (ent->run) {
        ent->run(ent);
    }
}
