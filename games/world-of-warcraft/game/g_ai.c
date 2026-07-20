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

/* Projectile damage belongs to its caster for retaliation, power gain, and XP credit. */
static LPEDICT Wow_CombatOwner(LPEDICT attacker) {
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

/* TODO: Replace this compact scaffold curve with authoritative server progression data. */
DWORD Wow_XpForNextLevel(DWORD level) {
    if (!level || level >= BZ_WOW_MAX_LEVEL) return 0;
    return 400 + (level - 1) * 500;
}

/* A kill is awarded once by Wow_AIDie; level-ups preserve overflow XP and refill the new pools. */
void Wow_AwardKillXp(LPEDICT attacker, LPEDICT victim) {
    wowEntityLocal_t *source_local, *victim_local;
    LPEDICT source = Wow_CombatOwner(attacker);
    DWORD needed;

    if (!source || !victim || source == victim) return;
    source_local = Wow_EntityLocal(source);
    victim_local = Wow_EntityLocal(victim);
    if (!source_local || !victim_local || source_local->kind != WOW_ENTITY_PLAYER ||
        victim_local->kind != WOW_ENTITY_CREATURE || !victim_local->xp_reward) return;
    source_local->xp += victim_local->xp_reward;
    while ((needed = Wow_XpForNextLevel(source_local->level)) && source_local->xp >= needed) {
        source_local->xp -= needed;
        source_local->level++;
        source_local->max_health = BZ_WOW_PLAYER_BASE_HEALTH +
            (source_local->level - 1) * BZ_WOW_PLAYER_HEALTH_PER_LEVEL;
        source_local->health = source_local->max_health;
        source_local->power = source_local->max_power;
        fprintf(stderr, "OpenWoW level: reached level %u\n", (unsigned)source_local->level);
    }
    if (source_local->level >= BZ_WOW_MAX_LEVEL) source_local->xp = 0;
    Wow_SyncEntityVitals(source);
}

static void Wow_GainCombatPower(LPEDICT ent, DWORD amount) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || local->kind != WOW_ENTITY_PLAYER || !local->max_power) return;
    local->power = MIN(local->power + amount, local->max_power);
    Wow_SyncEntityVitals(ent);
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
    if (!target_local || target_local->dead || target_local->health == 0) {
        return;
    }

    Wow_GainCombatPower(source, damage * 5);
    Wow_GainCombatPower(target, damage * 3);
    if (target_local->health <= damage) {
        Wow_AIDie(target, attacker);
        return;
    }

    target_local->health -= damage;
    Wow_SyncEntityVitals(target);

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
    if (!target || !target->inuse || target == ent) {
        local->enemy = NULL;
        return false;
    }
    target_local = Wow_EntityLocal(target);
    if (local->dead || (local->health == 0) ||
        (target_local && (target_local->dead || target_local->health == 0))) {
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
    ent->s.origin.x += delta.x * step / len;
    ent->s.origin.y += delta.y * step / len;
    ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
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
    (void)attacker;

    if (!ent || !local || local->dead) {
        return;
    }

    local->dead = true;
    local->health = 0;
    local->enemy = NULL;
    local->attack_time = 0;
    local->attack_damage_time = 0;
    local->attack_backswing_time = 0;
    local->attack_damage_done = false;
    local->pain_time = 0;

    ent->svflags |= SVF_DEADMONSTER;
    Wow_SyncEntityVitals(ent);
    Wow_AwardKillXp(attacker, ent);
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

            local->attack_damage_done = true;
            if (target) {
                Wow_DealDamage(target, ent, 1);
            }
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

void Wow_AIRunFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local) {
        return;
    }

    if (local->dead) {
        (void)Wow_AIAdvanceLockedFrame(ent);
        return;
    }

    if (Wow_AIAdvanceLockedFrame(ent)) {
        return;
    }

    if (local->patrol_radius > 0.0f && local->walk_speed > 0.0f) {
        local->patrol_phase += ((FLOAT)FRAMETIME / 1000.0f) * 0.6f;
        if (ent->move) {
            ent->move(ent);
        }
    } else {
        if (ent->idle) {
            ent->idle(ent);
        }
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
