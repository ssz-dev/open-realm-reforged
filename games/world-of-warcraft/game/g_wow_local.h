#ifndef G_WOW_LOCAL_H
#define G_WOW_LOCAL_H

#include "server/server.h"
#include "common/wow_ui_shared.h"
#include "common/ui_constants.h"

#define WOW_MAX_CLIENTS 1
#define WOW_MAX_EDICTS 128
#define WOW_PLAYER_MODEL "Character\\Orc\\Male\\OrcMale.m2"
#define WOW_PLAYER_WEAPON_MODEL "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01.m2"
#define WOW_CLASS_WARRIOR 1

/* CS_GENERAL slot used to pass selected character data from UI to game module.
   Set by the UI via a single userinfo-style cvar before map load, read by
   Wow_Init.  Format: \race\Human\sex\Male\class\1\appearance\12345 */
#define WOW_CS_PLAYERINFO 0
#define WOW_MOVE_FORWARD 1
#define WOW_MOVE_BACK 2
#define WOW_MOVE_LEFT 4
#define WOW_MOVE_RIGHT 8
#define WOW_WALK_SPEED 7.0f
#define BZ_WOW_GM_SPEED 70.0f
#define BZ_WOW_MAX_LEVEL 60
#define BZ_WOW_PLAYER_BASE_HEALTH 100
#define BZ_WOW_PLAYER_HEALTH_PER_LEVEL 10
#define BZ_WOW_PLAYER_MAX_POWER 100
#define BZ_WOW_CREATURE_BASE_HEALTH 3
#define BZ_WOW_CREATURE_KILL_XP 100
#define BZ_WOW_CREATURE_AGGRO_RANGE 14.0f
#define BZ_WOW_CREATURE_LEASH_RANGE 35.0f
#define BZ_WOW_CREATURE_CHASE_SPEED 5.0f
#define BZ_WOW_CREATURE_EVADE_SPEED 8.0f
#define BZ_WOW_CREATURE_REGEN_TIME 500
#define BZ_WOW_CREATURE_RESPAWN_TIME 5000
#define BZ_WOW_CREATURE_ATTACK_DAMAGE 1
#define BZ_WOW_STRIKE_DAMAGE 1
#define BZ_WOW_HEAVY_STRIKE_DAMAGE 3
#define BZ_WOW_HEAVY_STRIKE_RAGE 20
#define BZ_WOW_HEAVY_STRIKE_COOLDOWN 1500
#define BZ_WOW_THROW_DAMAGE 2
#define BZ_WOW_THROW_COOLDOWN 2000
#define BZ_WOW_THROW_RANGE 30.0f
#define BZ_WOW_COMBAT_MESSAGE_TIME 1500
#define WOW_MELEE_RANGE 5.0f
#define WOW_CAMERA_MIN_PITCH 300.0f
#define WOW_CAMERA_MAX_PITCH 350.0f
#define WOW_CAMERA_MIN_DISTANCE 3.0f
#define WOW_CAMERA_MAX_DISTANCE 35.0f

typedef enum {
    WOW_ENTITY_NONE,
    WOW_ENTITY_PLAYER,
    WOW_ENTITY_CREATURE,
    WOW_ENTITY_PROJECTILE,
} wowEntityKind_t;

typedef enum {
    WOW_AI_IDLE,
    WOW_AI_AGGRO,
    WOW_AI_CHASE,
    WOW_AI_ATTACK,
    WOW_AI_EVADE,
    WOW_AI_DEAD,
    WOW_AI_RESPAWN,
} wowAiState_t;

typedef enum {
    WOW_ABILITY_STRIKE,
    WOW_ABILITY_HEAVY_STRIKE,
    WOW_ABILITY_THROW,
    WOW_ABILITY_COUNT,
} wowAbility_t;

typedef enum {
    WOW_ACTION_DISABLED_DEAD = 1 << 0,
    WOW_ACTION_DISABLED_NO_TARGET = 1 << 1,
    WOW_ACTION_DISABLED_TARGET_DEAD = 1 << 2,
    WOW_ACTION_DISABLED_RANGE = 1 << 3,
    WOW_ACTION_DISABLED_RAGE = 1 << 4,
    WOW_ACTION_DISABLED_COOLDOWN = 1 << 5,
} wowActionFlags_t;

typedef enum {
    WOW_COMBAT_MESSAGE_NONE,
    WOW_COMBAT_MESSAGE_DAMAGE_DEALT,
    WOW_COMBAT_MESSAGE_DAMAGE_TAKEN,
    WOW_COMBAT_MESSAGE_XP,
    WOW_COMBAT_MESSAGE_LEVEL,
    WOW_COMBAT_MESSAGE_NO_RAGE,
    WOW_COMBAT_MESSAGE_OUT_OF_RANGE,
    WOW_COMBAT_MESSAGE_NO_TARGET,
    WOW_COMBAT_MESSAGE_NOT_READY,
} wowCombatMessageType_t;

typedef struct wowMove_s {
    LPCSTR animation;
    void (*think)(LPEDICT ent);
    void (*endfunc)(LPEDICT ent);
} wowMove_t, *LPWOWMOVE;

typedef struct {
    wowEntityKind_t kind;
    wowAiState_t ai_state;
    DWORD display_id;
    LPCANIMATION animation;
    LPWOWMOVE currentmove;
    VECTOR2 home;
    FLOAT yaw;
    FLOAT patrol_radius;
    FLOAT patrol_phase;
    FLOAT walk_speed;
    DWORD health;
    DWORD max_health;
    DWORD power;
    DWORD max_power;
    DWORD level;
    DWORD xp;
    DWORD xp_reward;
    DWORD attack_damage;
    DWORD attack_damage_point;
    DWORD attack_backswing;
    DWORD attack_time;
    DWORD attack_damage_time;
    DWORD attack_backswing_time;
    DWORD pain_time;
    DWORD death_time;
    DWORD respawn_time;
    DWORD regen_time;
    DWORD ability_cooldown[WOW_ABILITY_COUNT];
    BOOL attack_damage_done;
    BOOL dead;
    BOOL hostile;
    LPEDICT enemy;
    /* Projectile fields (valid when kind == WOW_ENTITY_PROJECTILE) */
    DWORD projectile_target;
    DWORD projectile_caster;
    FLOAT projectile_speed;
    DWORD projectile_damage;
    FLOAT projectile_yaw;
    FLOAT projectile_pitch;
} wowEntityLocal_t;

typedef struct {
    char icon[256];
    char name[64];
    DWORD count;
    DWORD rage_cost;
    DWORD cooldown;
    DWORD flags;
} wowHudIcon_t;

typedef struct {
    wowCombatMessageType_t type;
    DWORD time;
    char text[64];
} WOWCOMBATMESSAGE;
typedef WOWCOMBATMESSAGE *LPWOWCOMBATMESSAGE;
typedef WOWCOMBATMESSAGE const *LPCWOWCOMBATMESSAGE;

typedef struct {
    struct client_s client;
    UINAME name;
    char zone_name[128];
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    wowHudIcon_t actions[WOW_UI_ACTION_SLOTS];
    WOWCOMBATMESSAGE combat_message;
    DWORD ui_flags;
    BOOL quest_log_open;
} wowClient_t;

extern struct game_import gi;
extern struct game_export globals;
extern edict_t wow_edicts[WOW_MAX_EDICTS];
extern wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
extern wowClient_t wow_clients[WOW_MAX_CLIENTS];

int          G_RegisterModel(LPCSTR filename);
LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);

FLOAT Wow_Clamp(FLOAT value, FLOAT min_value, FLOAT max_value);
DWORD Wow_Read32(BYTE const *p);
FLOAT Wow_ReadFloat(BYTE const *p);
LPCSTR Wow_DbcString(BYTE const *string_block, DWORD string_size, DWORD offset);
BOOL Wow_ValidDbc(BYTE const *data,
                  DWORD size,
                  DWORD *records,
                  DWORD *fields,
                  DWORD *record_size,
                  DWORD *string_size);
BOOL Wow_FindDbcRecord(LPCSTR filename,
                       DWORD wanted_id,
                       LPBYTE *data_out,
                       DWORD *fields_out,
                       DWORD *record_size_out,
                       BYTE const **record_out,
                       BYTE const **strings_out,
                       DWORD *string_size_out);
FLOAT Wow_TerrainHeight(FLOAT x, FLOAT y);
LPCSTR CM_WowAreaNameAtPoint(FLOAT x, FLOAT y);
DWORD Wow_EntityIndex(LPCEDICT ent);
wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent);
LPCANIMATION Wow_SetEntityAnimation(LPEDICT ent, LPCSTR animation_name);
BOOL Wow_SetEntityMove(LPEDICT ent, LPWOWMOVE move);
BOOL Wow_SetEntityMoveFirstAnimation(LPEDICT ent, LPWOWMOVE move, LPCSTR const *animation_names);
void Wow_AdvanceEntityFrame(LPEDICT ent);
LPEDICT Wow_Spawn(void);
void Wow_AIIdle(LPEDICT ent);
void Wow_AIMove(LPEDICT ent);
void Wow_AIAttack(LPEDICT ent);
void Wow_AIPain(LPEDICT ent);
void Wow_AIDie(LPEDICT ent, LPEDICT attacker);
BOOL Wow_AIAdvanceLockedFrame(LPEDICT ent);
BOOL Wow_EntityAffectingCombat(LPEDICT ent);
BOOL Wow_EntityCanBeTargeted(LPCEDICT ent);
FLOAT Wow_Distance2(LPCVECTOR2 a, LPCVECTOR2 b);
void Wow_DealDamage(LPEDICT target, LPEDICT attacker, DWORD damage);
void Wow_SetCombatMessage(LPEDICT player, wowCombatMessageType_t type, DWORD value);
void Wow_SyncEntityVitals(LPEDICT ent);
DWORD Wow_XpForNextLevel(DWORD level);
void Wow_AwardKillXp(LPEDICT attacker, LPEDICT victim);
BOOL Wow_SetStandMove(LPEDICT ent);
BOOL Wow_SetRunMove(LPEDICT ent);
BOOL Wow_SetWalkMove(LPEDICT ent);
BOOL Wow_SetCombatReadyAnimation(LPEDICT ent);
void Wow_AIRunFrame(LPEDICT ent);
void Wow_SpawnAmbientCreatures(LPCVECTOR2 origin);
void Wow_RunCreatureFrame(LPEDICT ent);
void UI_WriteWowHud(LPEDICT ent);
void UI_WriteWowQuestLog(LPEDICT ent);
void UI_HideWowQuestLog(LPEDICT ent);

/* Ability/projectile system */
DWORD      Wow_FireboltModel(void);
void       Wow_RunProjectile(LPEDICT ent);
BOOL       Wow_FireFirebolt(LPEDICT caster, LPEDICT target);
void       Wow_HealingTouch(LPEDICT caster);
LPEDICT    Wow_FindSpellTarget(LPEDICT ent, FLOAT range);
BOOL       Wow_UseAbility(LPEDICT ent, wowAbility_t ability);

#endif
