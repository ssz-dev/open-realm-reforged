#include "g_wow_local.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define BZ_WOW_FILE_SYNC(file) _commit(_fileno(file))
#else
#include <unistd.h>
#define BZ_WOW_FILE_SYNC(file) fsync(fileno(file))
#endif

#define BZ_WOW_PROGRESS_MAGIC "OPENWOW_PROGRESS"
#define BZ_WOW_PROGRESS_VERSION 1
#define BZ_WOW_PROGRESS_CVAR "wow_save"

typedef struct {
    DWORD version;
    DWORD level;
    DWORD xp;
    DWORD health;
    DWORD max_health;
    DWORD power;
    WOWITEMSTACK bag[WOW_UI_INVENTORY_SLOTS];
    wowItemId_t equipment[WOW_EQUIPMENT_COUNT];
    WOWQUESTPROGRESS quest;
} WOWPROGRESS;
typedef WOWPROGRESS *LPWOWPROGRESS;
typedef WOWPROGRESS const *LPCWOWPROGRESS;

static BOOL wow_progress_autosave_allowed;

static LPCSTR Wow_ProgressPath(void) { return gi.CvarString(BZ_WOW_PROGRESS_CVAR, ""); }

/* Persistence accepts only the one authoritative local player and never serializes edict-owned transient state. */
static wowClient_t *Wow_ProgressClient(LPEDICT player, wowEntityLocal_t **local_out) {
    wowEntityLocal_t *local = Wow_EntityLocal(player);

    if (local_out) *local_out = local;
    return player && player->client && local && local->kind == WOW_ENTITY_PLAYER
        ? (wowClient_t *)player->client : NULL;
}

static void Wow_DefaultProgress(LPWOWPROGRESS progress) {
    memset(progress, 0, sizeof(*progress));
    progress->version = BZ_WOW_PROGRESS_VERSION;
    progress->level = 1;
    progress->health = progress->max_health = BZ_WOW_PLAYER_BASE_HEALTH;
    progress->quest.id = WOW_QUEST_FIRST_HUNT;
    progress->quest.state = WOW_QUEST_AVAILABLE;
}

static BOOL Wow_ProgressHasItem(LPCWOWPROGRESS progress, wowItemId_t item) {
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        if (progress->bag[i].item == item && progress->bag[i].count) return true;
    return false;
}

/* Validation accepts clamped live vitals but rejects structural inventory, equipment, XP, and quest corruption. */
static BOOL Wow_ValidateProgress(LPWOWPROGRESS progress) {
    LPCWOWQUESTDEF quest;
    DWORD expected_health, needed;

    if (!progress || progress->version != BZ_WOW_PROGRESS_VERSION ||
        !progress->level || progress->level > BZ_WOW_MAX_LEVEL) return false;
    expected_health = BZ_WOW_PLAYER_BASE_HEALTH +
        (progress->level - 1) * BZ_WOW_PLAYER_HEALTH_PER_LEVEL;
    if (progress->max_health != expected_health) return false;
    needed = Wow_XpForNextLevel(progress->level);
    if ((needed && progress->xp >= needed) || (!needed && progress->xp)) return false;
    progress->health = MIN(progress->health, progress->max_health);
    progress->power = MIN(progress->power, BZ_WOW_PLAYER_MAX_POWER);
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        LPCWOWITEMDEF item = Wow_ItemDef(progress->bag[i].item);

        if (progress->bag[i].item == WOW_ITEM_NONE) {
            if (progress->bag[i].count) return false;
        } else if (!item || !progress->bag[i].count || progress->bag[i].count > item->max_stack) {
            return false;
        }
    }
    FOR_LOOP(i, WOW_EQUIPMENT_COUNT) {
        LPCWOWITEMDEF item = Wow_ItemDef(progress->equipment[i]);
        wowItemType_t required = i == WOW_EQUIPMENT_WEAPON ? WOW_ITEM_WEAPON : WOW_ITEM_ARMOR;

        if (progress->equipment[i] == WOW_ITEM_NONE) continue;
        if (!item || item->type != required || !Wow_ProgressHasItem(progress, item->id)) return false;
    }
    quest = Wow_QuestDef(progress->quest.id);
    if (!quest || progress->quest.count > quest->required_count) return false;
    switch (progress->quest.state) {
        case WOW_QUEST_AVAILABLE:
        case WOW_QUEST_UNAVAILABLE:
            return progress->quest.count == 0;
        case WOW_QUEST_ACTIVE:
            return progress->quest.count < quest->required_count;
        case WOW_QUEST_READY_TO_TURN_IN:
        case WOW_QUEST_COMPLETED:
            return progress->quest.count == quest->required_count;
        default:
            return false;
    }
}

static BOOL Wow_CaptureProgress(LPEDICT player, LPWOWPROGRESS progress) {
    wowEntityLocal_t *local;
    wowClient_t *client = Wow_ProgressClient(player, &local);

    if (!client || !progress) return false;
    memset(progress, 0, sizeof(*progress));
    progress->version = BZ_WOW_PROGRESS_VERSION;
    progress->level = local->level;
    progress->xp = local->xp;
    progress->health = local->health;
    progress->max_health = local->max_health;
    progress->power = local->power;
    memcpy(progress->bag, client->bag, sizeof(progress->bag));
    memcpy(progress->equipment, client->equipment, sizeof(progress->equipment));
    progress->quest = client->quest;
    return Wow_ValidateProgress(progress);
}

/* Applying a validated snapshot updates authoritative gameplay fields and invalidates only derived HUD caches. */
static void Wow_ApplyProgress(LPEDICT player, LPCWOWPROGRESS progress) {
    wowEntityLocal_t *local;
    wowClient_t *client = Wow_ProgressClient(player, &local);

    if (!client || !progress) return;
    local->level = progress->level;
    local->xp = progress->xp;
    local->health = progress->health;
    local->max_health = progress->max_health;
    local->power = progress->power;
    local->max_power = BZ_WOW_PLAYER_MAX_POWER;
    memcpy(client->bag, progress->bag, sizeof(client->bag));
    memcpy(client->equipment, progress->equipment, sizeof(client->equipment));
    client->quest = progress->quest;
    memset(client->inventory, 0, sizeof(client->inventory));
    memset(&client->quest_hud, 0, sizeof(client->quest_hud));
    client->equipment_text[0] = '\0';
    client->loot_target = 0;
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    Wow_SyncEntityVitals(player);
}

static BOOL Wow_WriteProgressBody(FILE *file, LPCWOWPROGRESS progress) {
    if (fprintf(file, "%s %u\n", BZ_WOW_PROGRESS_MAGIC, (unsigned)progress->version) < 0 ||
        fprintf(file, "player %u %u\n", (unsigned)progress->level, (unsigned)progress->xp) < 0 ||
        fprintf(file, "vitals %u %u %u\n", (unsigned)progress->health,
                (unsigned)progress->max_health, (unsigned)progress->power) < 0 ||
        fprintf(file, "bag %u\n", (unsigned)WOW_UI_INVENTORY_SLOTS) < 0) return false;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        if (fprintf(file, "slot %u %u %u\n", (unsigned)i, (unsigned)progress->bag[i].item,
                    (unsigned)progress->bag[i].count) < 0) return false;
    return fprintf(file, "equipment %u %u\n", (unsigned)progress->equipment[WOW_EQUIPMENT_WEAPON],
                   (unsigned)progress->equipment[WOW_EQUIPMENT_ARMOR]) >= 0 &&
        fprintf(file, "quest %u %u %u\n", (unsigned)progress->quest.id,
                (unsigned)progress->quest.state, (unsigned)progress->quest.count) >= 0 &&
        fprintf(file, "end\n") >= 0;
}

/* The destination changes only after a complete flushed temporary snapshot is ready for atomic rename. */
static BOOL Wow_WriteProgress(LPCSTR path, LPCWOWPROGRESS progress) {
    char temporary[MAX_PATHLEN];
    FILE *file;
    BOOL written;

    if (!path || !*path ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return false;
    file = fopen(temporary, "wb");
    if (!file) return false;
    written = Wow_WriteProgressBody(file, progress);
    if (written && (fflush(file) || BZ_WOW_FILE_SYNC(file))) written = false;
    if (fclose(file)) written = false;
    if (!written) {
        remove(temporary);
        return false;
    }
    if (rename(temporary, path)) {
        remove(temporary);
        return false;
    }
    return true;
}

/* Parse the fixed schema into a detached snapshot so malformed files cannot partially mutate runtime state. */
static wowProgressLoadResult_t Wow_ReadProgress(LPCSTR path, LPWOWPROGRESS progress) {
    char magic[32], token[32], trailing[2];
    unsigned version, level, xp, health, max_health, power, slots;
    unsigned weapon, armor, quest, quest_state, quest_count;
    FILE *file = fopen(path, "rb");
    wowProgressLoadResult_t result = WOW_PROGRESS_LOAD_INVALID;

    if (!file) return errno == ENOENT ? WOW_PROGRESS_LOAD_MISSING : WOW_PROGRESS_LOAD_IO_ERROR;
    memset(progress, 0, sizeof(*progress));
    if (fscanf(file, "%31s %u", magic, &version) != 2 || strcmp(magic, BZ_WOW_PROGRESS_MAGIC)) goto done;
    if (version != BZ_WOW_PROGRESS_VERSION) {
        result = WOW_PROGRESS_LOAD_UNSUPPORTED;
        goto done;
    }
    progress->version = version;
    if (fscanf(file, "%31s %u %u", token, &level, &xp) != 3 || strcmp(token, "player") ||
        fscanf(file, "%31s %u %u %u", token, &health, &max_health, &power) != 4 ||
        strcmp(token, "vitals") ||
        fscanf(file, "%31s %u", token, &slots) != 2 || strcmp(token, "bag") ||
        slots != WOW_UI_INVENTORY_SLOTS) goto done;
    progress->level = level;
    progress->xp = xp;
    progress->health = health;
    progress->max_health = max_health;
    progress->power = power;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        unsigned slot, item, count;

        if (fscanf(file, "%31s %u %u %u", token, &slot, &item, &count) != 4 ||
            strcmp(token, "slot") || slot != i) goto done;
        progress->bag[i] = (WOWITEMSTACK){ (wowItemId_t)item, count };
    }
    if (fscanf(file, "%31s %u %u", token, &weapon, &armor) != 3 || strcmp(token, "equipment") ||
        fscanf(file, "%31s %u %u %u", token, &quest, &quest_state, &quest_count) != 4 ||
        strcmp(token, "quest") ||
        fscanf(file, "%31s", token) != 1 || strcmp(token, "end") ||
        fscanf(file, " %1s", trailing) != EOF || ferror(file)) goto done;
    progress->equipment[WOW_EQUIPMENT_WEAPON] = (wowItemId_t)weapon;
    progress->equipment[WOW_EQUIPMENT_ARMOR] = (wowItemId_t)armor;
    progress->quest = (WOWQUESTPROGRESS){ (wowQuestId_t)quest, (wowQuestState_t)quest_state, quest_count };
    result = Wow_ValidateProgress(progress) ? WOW_PROGRESS_LOAD_OK : WOW_PROGRESS_LOAD_INVALID;

done:
    fclose(file);
    return result;
}

void Wow_InitProgressPersistence(void) { wow_progress_autosave_allowed = false; }

/* Runtime defaults are the same path for a new player and a confirmed reset command. */
void Wow_ResetPlayerProgress(LPEDICT player) {
    wowEntityLocal_t *local;
    wowClient_t *client = Wow_ProgressClient(player, &local);
    WOWPROGRESS progress;

    if (!client) return;
    Wow_DefaultProgress(&progress);
    Wow_ApplyProgress(player, &progress);
    local->enemy = NULL;
    memset(local->ability_cooldown, 0, sizeof(local->ability_cooldown));
    memset(&client->combat_message, 0, sizeof(client->combat_message));
}

wowProgressLoadResult_t Wow_LoadPlayerProgress(LPEDICT player) {
    LPCSTR path = Wow_ProgressPath();
    WOWPROGRESS progress;
    wowProgressLoadResult_t result;

    if (!path || !*path) return WOW_PROGRESS_LOAD_DISABLED;
    result = Wow_ReadProgress(path, &progress);
    wow_progress_autosave_allowed = result == WOW_PROGRESS_LOAD_OK || result == WOW_PROGRESS_LOAD_MISSING;
    if (result == WOW_PROGRESS_LOAD_OK) {
        Wow_ApplyProgress(player, &progress);
        fprintf(stderr, "OpenWoW progress: loaded %s\n", path);
    } else if (result == WOW_PROGRESS_LOAD_MISSING) {
        fprintf(stderr, "OpenWoW progress: no save at %s, starting new progress\n", path);
    } else if (result == WOW_PROGRESS_LOAD_UNSUPPORTED) {
        fprintf(stderr, "OpenWoW progress: unsupported future save version in %s\n", path);
    } else {
        fprintf(stderr, "OpenWoW progress: invalid or unreadable save %s\n", path);
    }
    return result;
}

/* Explicit saves may replace a rejected file, while automatic saves remain guarded until the player chooses that. */
BOOL Wow_SavePlayerProgress(LPEDICT player) {
    LPCSTR path = Wow_ProgressPath();
    WOWPROGRESS progress;

    if (!path || !*path) {
        fprintf(stderr, "OpenWoW progress: no save path configured\n");
        return false;
    }
    if (!Wow_CaptureProgress(player, &progress) || !Wow_WriteProgress(path, &progress)) {
        fprintf(stderr, "OpenWoW progress: failed to save %s\n", path);
        return false;
    }
    wow_progress_autosave_allowed = true;
    fprintf(stderr, "OpenWoW progress: saved %s\n", path);
    return true;
}

/* A rejected snapshot is preserved across shutdown so the fallback defaults never overwrite evidence or newer data. */
BOOL Wow_AutoSavePlayerProgress(LPEDICT player) {
    LPCSTR path = Wow_ProgressPath();

    if (!path || !*path || !wow_progress_autosave_allowed) return true;
    return Wow_SavePlayerProgress(player);
}

/* A reset replaces the disk snapshot first, then commits the same default snapshot to runtime. */
BOOL Wow_ResetSavedProgress(LPEDICT player) {
    LPCSTR path = Wow_ProgressPath();
    wowEntityLocal_t *local;
    wowClient_t *client = Wow_ProgressClient(player, &local);
    WOWPROGRESS progress;

    if (!client || !path || !*path) return false;
    Wow_DefaultProgress(&progress);
    if (!Wow_WriteProgress(path, &progress)) {
        fprintf(stderr, "OpenWoW progress: failed to reset %s\n", path);
        return false;
    }
    Wow_ApplyProgress(player, &progress);
    local->enemy = NULL;
    memset(local->ability_cooldown, 0, sizeof(local->ability_cooldown));
    memset(&client->combat_message, 0, sizeof(client->combat_message));
    wow_progress_autosave_allowed = true;
    fprintf(stderr, "OpenWoW progress: reset %s\n", path);
    return true;
}
