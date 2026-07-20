#include "test_framework.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

typedef struct {
    char name[MAX_PATHLEN];
    int index;
} testModel_t;

static testModel_t test_models[32];
static testModel_t test_images[32];
static DWORD test_num_models;
static DWORD test_num_images;
static DWORD test_clear_world_calls;
static DWORD test_apply_lobby_calls;
static BYTE test_multicast_buf[MAX_MSGLEN];
static BYTE test_last_unicast_buf[MAX_MSGLEN];
static DWORD test_multicast_size;
static DWORD test_last_unicast_size;
static DWORD test_unicast_calls;
static DWORD test_hud_write_calls;
static DWORD test_quest_show_calls;
static DWORD test_quest_hide_calls;
static char test_last_error[512];
static char test_save_path[MAX_PATHLEN];

#define TEST_PROGRESS_PATH "build/tests/openwow-progress-test.sav"

/* ---- configstring stubs (game_import.configstring / GetConfigstring) ---- */
#define TEST_CONFIGSTRINGS 128
static char test_configstrings[TEST_CONFIGSTRINGS][512];

static void test_configstring(DWORD index, LPCSTR string) {
    if (index < TEST_CONFIGSTRINGS) {
        strncpy(test_configstrings[index], string ? string : "", sizeof(test_configstrings[index]) - 1);
        test_configstrings[index][sizeof(test_configstrings[index]) - 1] = '\0';
    }
}

static LPCSTR test_get_configstring(DWORD index) {
    if (index < TEST_CONFIGSTRINGS) {
        return test_configstrings[index];
    }
    return "";
}

/* ---- cvar stub ---- */
static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
    if (name && !strcasecmp(name, "wow_save")) return test_save_path;
    return fallback ? fallback : "";
}

static animation_t test_animations[] = {
    { .name = "Stand",        .interval = { 0, 1000 } },
    { .name = "Walk",         .interval = { 0, 1000 } },
    { .name = "Run",          .interval = { 0, 1000 } },
    { .name = "Ready1H",      .interval = { 0, 1000 } },
    { .name = "ReadyUnarmed", .interval = { 0, 1000 } },
    { .name = "Attack1H",     .interval = { 0, 1000 } },
    { .name = "SpellCastOmni", .interval = { 0, 1000 } },
    { .name = "Pain",         .interval = { 0,  450 } },
    { .name = "Death",        .interval = { 0, 1200 } },
};

static void put32(LPBYTE out, DWORD value) {
    out[0] = (BYTE)(value & 0xff);
    out[1] = (BYTE)((value >> 8) & 0xff);
    out[2] = (BYTE)((value >> 16) & 0xff);
    out[3] = (BYTE)((value >> 24) & 0xff);
}

static void putfloat(LPBYTE out, FLOAT value) {
    memcpy(out, &value, sizeof(value));
}

static void putfield(LPBYTE record, DWORD field, DWORD value) {
    put32(record + field * sizeof(DWORD), value);
}

static void putfield_float(LPBYTE record, DWORD field, FLOAT value) {
    putfloat(record + field * sizeof(DWORD), value);
}

static HANDLE alloc_dbc(DWORD records, DWORD fields, DWORD string_size, LPDWORD size_out) {
    DWORD record_size = fields * sizeof(DWORD);
    DWORD size = 20 + records * record_size + string_size;
    LPBYTE data = calloc(1, size);

    memcpy(data, "WDBC", 4);
    put32(data + 4, records);
    put32(data + 8, fields);
    put32(data + 12, record_size);
    put32(data + 16, string_size);
    *size_out = size;
    return data;
}

static DWORD add_string(LPBYTE strings, DWORD *cursor, LPCSTR value) {
    DWORD offset = *cursor;
    DWORD len = (DWORD)strlen(value) + 1;

    memcpy(strings + offset, value, len);
    *cursor += len;
    return offset;
}

static HANDLE make_map_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 5, 64, &size);
    LPBYTE record = data + 20;
    LPBYTE strings = record + 5 * sizeof(DWORD);
    DWORD cursor = 1;
    DWORD map_name = add_string(strings, &cursor, "Azeroth");
    DWORD title = add_string(strings, &cursor, "Elwynn Test");

    putfield(record, 0, 1);
    putfield(record, 1, map_name);
    putfield(record, 3, title);
    putfield(record, 4, 42);
    *size_out = size;
    return data;
}

static HANDLE make_loading_screens_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 3, 96, &size);
    LPBYTE record = data + 20;
    LPBYTE strings = record + 3 * sizeof(DWORD);
    DWORD cursor = 1;
    DWORD texture = add_string(strings, &cursor, "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");

    putfield(record, 0, 42);
    putfield(record, 2, texture);
    *size_out = size;
    return data;
}

static HANDLE make_world_safe_locs_dbc(LPDWORD size_out) {
    static struct {
        DWORD id;
        LPCSTR name;
        FLOAT x, y, z;
    } const safe_locs[] = {
        { 100, "Northshire", 123.25f, -456.5f, 78.0f },
        { 101, "Deathknell, Tirisfal", 1880.7385f, 1624.7355f, 94.4343f },
        { 102, "Coldridge Valley", -6240.32f, 331.033f, 382.758f },
        { 103, "Valley of Trials", -600.0f, -4200.0f, 38.0f },
    };
    DWORD size;
    LPBYTE data = alloc_dbc(sizeof(safe_locs) / sizeof(safe_locs[0]), 6, 128, &size);
    LPBYTE records = data + 20;
    LPBYTE strings = records + sizeof(safe_locs) / sizeof(safe_locs[0]) * 6 * sizeof(DWORD);
    DWORD cursor = 1;

    FOR_LOOP(i, sizeof(safe_locs) / sizeof(safe_locs[0])) {
        LPBYTE record = records + i * 6 * sizeof(DWORD);
        DWORD safe_name = add_string(strings, &cursor, safe_locs[i].name);

        putfield(record, 0, safe_locs[i].id);
        putfield(record, 1, 1);
        putfield_float(record, 2, safe_locs[i].x);
        putfield_float(record, 3, safe_locs[i].y);
        putfield_float(record, 4, safe_locs[i].z);
        putfield(record, 5, safe_name);
    }
    *size_out = size;
    return data;
}

static HANDLE make_creature_display_info_dbc(LPDWORD size_out) {
    DWORD displays[] = { 161, 193, 163, 188 };
    DWORD size;
    LPBYTE data = alloc_dbc(4, 5, 1, &size);
    LPBYTE records = data + 20;

    FOR_LOOP(i, 4) {
        LPBYTE record = records + i * 5 * sizeof(DWORD);

        putfield(record, 0, displays[i]);
        putfield(record, 1, 700 + i);
        putfield_float(record, 4, 1.0f);
    }
    *size_out = size;
    return data;
}

static HANDLE make_creature_model_data_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(4, 15, 160, &size);
    LPBYTE records = data + 20;
    LPBYTE strings = records + 4 * 15 * sizeof(DWORD);
    DWORD cursor = 1;

    FOR_LOOP(i, 4) {
        LPBYTE record = records + i * 15 * sizeof(DWORD);
        char model_name[64];
        DWORD model_offset;

        snprintf(model_name, sizeof(model_name), "Creature\\Test\\Creature%u.m2", (unsigned)i);
        model_offset = add_string(strings, &cursor, model_name);
        putfield(record, 0, 700 + i);
        putfield(record, 2, model_offset);
        putfield_float(record, 4, 1.0f);
        putfield_float(record, 14, 3.0f);
    }
    *size_out = size;
    return data;
}

static HANDLE make_area_table_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 21, 32, &size);
    LPBYTE record = data + 20;
    LPBYTE strings = record + 21 * sizeof(DWORD);
    DWORD cursor = 1;
    DWORD name = add_string(strings, &cursor, "Test Valley");

    putfield(record, 0, 228);
    putfield(record, 11, name);
    *size_out = size;
    return data;
}

static HANDLE make_area_adt(LPDWORD size_out) {
    DWORD chunk_size = 0x80;
    LPBYTE data = calloc(1, 8 + chunk_size);
    LPBYTE chunk = data + 8;

    memcpy(data, "KNCM", 4);
    put32(data + 4, chunk_size);
    put32(chunk + 0x04, 10);
    put32(chunk + 0x08, 13);
    put32(chunk + 0x34, 228);
    putfloat(chunk + 0x68, 133.0f);
    putfloat(chunk + 0x6c, 233.0f);
    *size_out = 8 + chunk_size;
    return data;
}

static BOOL path_eq(LPCSTR a, LPCSTR b) {
    while (*a && *b) {
        char ca = *a == '/' ? '\\' : *a;
        char cb = *b == '/' ? '\\' : *b;

        if (tolower((unsigned char)ca) != tolower((unsigned char)cb)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static HANDLE test_read_file(LPCSTR filename, LPDWORD size) {
    static BYTE artificial_firebolt;

    if (path_eq(filename, "DBFilesClient\\Map.dbc")) {
        return make_map_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\LoadingScreens.dbc")) {
        return make_loading_screens_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\WorldSafeLocs.dbc")) {
        return make_world_safe_locs_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\CreatureDisplayInfo.dbc")) {
        return make_creature_display_info_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\CreatureModelData.dbc")) {
        return make_creature_model_data_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\AreaTable.dbc")) {
        return make_area_table_dbc(size);
    }
    if (path_eq(filename, "World\\Maps\\Azeroth\\Azeroth_31_31.adt")) {
        return make_area_adt(size);
    }
    if (path_eq(filename, "Spells\\Fireball_Missile_High.m2")) {
        if (size) *size = 1;
        return &artificial_firebolt;
    }
    if (size) {
        *size = 0;
    }
    return NULL;
}

static HANDLE test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(HANDLE mem) {
    free(mem);
}

static int test_model_index(LPCSTR model_name) {
    FOR_LOOP(i, test_num_models) {
        if (!strcasecmp(test_models[i].name, model_name)) {
            return test_models[i].index;
        }
    }
    ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
    strncpy(test_models[test_num_models].name, model_name, sizeof(test_models[0].name) - 1);
    test_models[test_num_models].index = (int)test_num_models + 1;
    test_num_models++;
    return (int)test_num_models;
}

static int test_image_index(LPCSTR image_name) {
    FOR_LOOP(i, test_num_images) {
        if (!strcasecmp(test_images[i].name, image_name)) {
            return test_images[i].index;
        }
    }
    ASSERT(test_num_images < sizeof(test_images) / sizeof(test_images[0]));
    strncpy(test_images[test_num_images].name, image_name, sizeof(test_images[0].name) - 1);
    test_images[test_num_images].index = (int)test_num_images + 1;
    test_num_images++;
    return (int)test_num_images;
}

static void test_clear_world(void) {
    test_clear_world_calls++;
}

static void test_apply_lobby_settings(LPMAPINFO info) {
    test_apply_lobby_calls++;
    ASSERT_NOT_NULL(info);
}

static void test_error(LPCSTR fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(test_last_error, sizeof(test_last_error), fmt, args);
    va_end(args);
}

void UI_WriteWowHud(LPEDICT ent) {
    ASSERT_NOT_NULL(ent);
    test_hud_write_calls++;
}

void UI_WriteWowQuestLog(LPEDICT ent) {
    ASSERT_NOT_NULL(ent);
    test_quest_show_calls++;
}

void UI_HideWowQuestLog(LPEDICT ent) {
    ASSERT_NOT_NULL(ent);
    test_quest_hide_calls++;
}

static void test_write_data(void const *data, DWORD size) {
    if (!data || test_multicast_size + size > sizeof(test_multicast_buf)) {
        return;
    }
    memcpy(test_multicast_buf + test_multicast_size, data, size);
    test_multicast_size += size;
}

static void test_write(pfWriteType_t type, void const *value) {
    BYTE b;
    SHORT s;
    LPCSTR text;

    switch (type) {
        case PF_BYTE:
            b = (BYTE)*(LONG const *)value;
            test_write_data(&b, sizeof(b));
            break;
        case PF_SHORT:
            s = (SHORT)*(LONG const *)value;
            test_write_data(&s, sizeof(s));
            break;
        case PF_STRING:
            text = value ? (LPCSTR)value : "";
            test_write_data(text, (DWORD)strlen(text) + 1);
            break;
        default:
            break;
    }
}

static void test_unicast(LPEDICT ent) {
    (void)ent;
    test_unicast_calls++;
    test_last_unicast_size = test_multicast_size;
    memcpy(test_last_unicast_buf, test_multicast_buf, test_last_unicast_size);
    test_multicast_size = 0;
}

static struct game_import test_import(void) {
    struct game_import import;

    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = test_image_index;
    import.ReadFile = test_read_file;
    import.ClearWorld = test_clear_world;
    import.ApplyLobbySettings = test_apply_lobby_settings;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    import.Write = test_write;
    import.unicast = test_unicast;
    import.error = test_error;
    return import;
}

static LPEDICT first_creature(void) {
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        wowEntityLocal_t *local = Wow_EntityLocal(&wow_edicts[i]);

        if (wow_edicts[i].inuse && local && local->kind == WOW_ENTITY_CREATURE) {
            return &wow_edicts[i];
        }
    }
    return NULL;
}

static LPEDICT quest_giver(void) {
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        wowEntityLocal_t *local = Wow_EntityLocal(&wow_edicts[i]);

        if (wow_edicts[i].inuse && local && local->quest_giver == WOW_QUEST_FIRST_HUNT)
            return &wow_edicts[i];
    }
    return NULL;
}

static DWORD hostile_creatures(LPEDICT *creatures, DWORD capacity) {
    DWORD count = 0;

    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts && count < capacity; i++) {
        wowEntityLocal_t *local = Wow_EntityLocal(&wow_edicts[i]);

        if (wow_edicts[i].inuse && local && local->kind == WOW_ENTITY_CREATURE && local->hostile)
            creatures[count++] = &wow_edicts[i];
    }
    return count;
}

static DWORD test_inventory_count(wowClient_t const *client, wowItemId_t item) {
    DWORD count = 0;

    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        if (client->bag[i].item == item) count += client->bag[i].count;
    return count;
}

static void test_progress_paths_clear(void) {
    char temporary[MAX_PATHLEN];

    snprintf(temporary, sizeof(temporary), "%s.tmp", TEST_PROGRESS_PATH);
    remove(temporary);
    rmdir(temporary);
    remove(TEST_PROGRESS_PATH);
}

static void test_progress_path_enable(void) {
    test_progress_paths_clear();
    snprintf(test_save_path, sizeof(test_save_path), "%s", TEST_PROGRESS_PATH);
}

static void test_write_local_file(LPCSTR path, LPCSTR text) {
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);

    ASSERT_NOT_NULL(file);
    if (!file) return;
    ASSERT_EQ_INT((int)fwrite(text, 1, length, file), (int)length);
    ASSERT_EQ_INT(fclose(file), 0);
}

static void test_read_local_file(LPCSTR path, LPSTR out, DWORD out_size) {
    FILE *file = fopen(path, "rb");
    size_t size;

    ASSERT_NOT_NULL(file);
    if (!file || !out || out_size < 2) return;
    size = fread(out, 1, out_size - 1, file);
    out[size] = '\0';
    ASSERT(!ferror(file));
    ASSERT_EQ_INT(fclose(file), 0);
}

int G_RegisterModel(LPCSTR filename) {
    return gi.ModelIndex(filename);
}

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname) {
    (void)modelindex;
    FOR_LOOP(i, sizeof(test_animations) / sizeof(test_animations[0])) {
        if (!strcasecmp(test_animations[i].name, animname)) {
            return &test_animations[i];
        }
    }
    return NULL;
}

void G_FreeModels(void) {
}

void PF_TextRemoveComments(LPSTR buffer) {
    (void)buffer;
}

static void reset_test_state(void) {
    memset(test_models, 0, sizeof(test_models));
    memset(test_images, 0, sizeof(test_images));
    test_num_models = 0;
    test_num_images = 0;
    test_clear_world_calls = 0;
    test_apply_lobby_calls = 0;
    memset(test_multicast_buf, 0, sizeof(test_multicast_buf));
    test_multicast_size = 0;
    memset(test_last_unicast_buf, 0, sizeof(test_last_unicast_buf));
    test_last_unicast_size = 0;
    test_unicast_calls = 0;
    test_hud_write_calls = 0;
    test_quest_show_calls = 0;
    test_quest_hide_calls = 0;
    memset(test_last_error, 0, sizeof(test_last_error));
    memset(test_configstrings, 0, sizeof(test_configstrings));
    test_save_path[0] = '\0';
}

static void assert_player_ui_payload(void) {
    DWORD cursor = 0;
    int num_buttons;
    int num_inventory;

    ASSERT(test_last_unicast_size > 0);
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], svc_unit_ui);
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], 1);
    ASSERT_EQ_INT((SHORT)(test_last_unicast_buf[cursor] | (test_last_unicast_buf[cursor + 1] << 8)), 0);
    cursor += 2;
    num_buttons = test_last_unicast_buf[cursor++];
    ASSERT_EQ_INT(num_buttons, WOW_UI_ACTION_SLOTS);
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Strike");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "1");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "wow_action 0");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], '1');
    for (int i = 1; i < num_buttons; i++) {
        FOR_LOOP(j, 4) {
            cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
        }
        cursor++;
    }
    num_inventory = test_last_unicast_buf[cursor++];
    ASSERT_EQ_INT(num_inventory, WOW_UI_INVENTORY_SLOTS);
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "0");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], 0);
}

static struct game_export *init_game(void) {
    struct game_import import = test_import();
    struct game_export *game;

    reset_test_state();
    game = GetGameAPI(&import);
    ASSERT_NOT_NULL(game);
    ASSERT_NOT_NULL(game->Init);
    ASSERT_NOT_NULL(game->LoadMap);
    game->Init();
    return game;
}

static void assert_player_spawned_at_safe_loc(LPEDICT player) {
    LPCMAPINFO info = CM_GetMapInfo();
    BOOL matched = false;

    ASSERT_NOT_NULL(info);
    ASSERT_STR_EQ(info->players[0].playerName, "Northshire");
    ASSERT_STR_EQ(info->players[1].playerName, "Deathknell, Tirisfal");
    ASSERT_EQ_INT((int)info->players[0].playerType, kPlayerTypeHuman);
    ASSERT_EQ_INT((int)info->players[1].playerType, kPlayerTypeNone);

    FOR_LOOP(i, MAX_PLAYERS) {
        LPCMAPPLAYER spawn = &info->players[i];

        if (!spawn->used)
            continue;
        if (fabsf(player->s.origin.x - spawn->startingPosition.x) > 0.001f ||
            fabsf(player->s.origin.y - spawn->startingPosition.y) > 0.001f)
            continue;
        ASSERT_EQ_INT((int)player->client->ps.start_location, (int)i);
        ASSERT_EQ_FLOAT(player->s.origin.z,
                        CM_GetHeightAtPoint(spawn->startingPosition.x, spawn->startingPosition.y),
                        0.001f);
        matched = true;
    }
    ASSERT(matched);
}

static void test_wow_load_map_initializes_player_state(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowEntityLocal_t *local;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);

    ASSERT_EQ_INT((int)test_apply_lobby_calls, 1);
    ASSERT_EQ_INT((int)test_clear_world_calls, 1);
    ASSERT(player->inuse);
    ASSERT_NOT_NULL(player->client);
    ASSERT_NOT_NULL(local);
    ASSERT_EQ_INT((int)local->kind, WOW_ENTITY_PLAYER);
    ASSERT_EQ_INT((int)local->health, 100);
    ASSERT_EQ_INT((int)local->max_health, 100);
    ASSERT_EQ_INT((int)local->power, 0);
    ASSERT_EQ_INT((int)local->max_power, BZ_WOW_PLAYER_MAX_POWER);
    ASSERT_EQ_INT((int)local->level, 1);
    ASSERT_EQ_INT((int)local->xp, 0);
    ASSERT_EQ_INT((int)((wowClient_t *)player->client)->quest.id, WOW_QUEST_FIRST_HUNT);
    ASSERT_EQ_INT((int)((wowClient_t *)player->client)->quest.state, WOW_QUEST_AVAILABLE);
    ASSERT_EQ_INT((int)player->s.stats[ENT_HEALTH], 255);
    assert_player_spawned_at_safe_loc(player);
    ASSERT_EQ_FLOAT(player->client->ps.origin.x, player->s.origin.x, 0.001f);
    ASSERT_EQ_FLOAT(player->client->ps.origin.y, player->s.origin.y, 0.001f);
    ASSERT_EQ_INT((int)player->client->ps.client_ui_state, CLIENT_UI_LOADING);
    ASSERT_STR_EQ(player->client->ps.name, "Thrall");
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH], 100);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH_MAX], 100);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_POWER], 0);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_POWER_MAX], BZ_WOW_PLAYER_MAX_POWER);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_LEVEL], 1);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_XP], 0);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_XP_MAX], 400);
    ASSERT_EQ_INT((int)test_num_images, 0);
    ASSERT_EQ_INT((int)test_unicast_calls, 0);
    ASSERT_NOT_NULL(game->ClientBegin);
    game->ClientBegin(player);
    ASSERT_EQ_INT((int)player->client->ps.client_ui_state, CLIENT_UI_GAME);
    ASSERT(test_unicast_calls > 0);
    assert_player_ui_payload();
    ASSERT_STR_EQ(player->client->ps.texts[PLAYERTEXT_MAP_TITLE], "Elwynn Test");
    ASSERT_STR_EQ(player->client->ps.texts[PLAYERTEXT_MAP_PREVIEW],
                  "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");
    ASSERT(player->s.model > 0);
    ASSERT(player->s.model2 > 0);
    ASSERT_EQ_FLOAT(player->s.angle, 0.0f, 0.001f);
    ASSERT_EQ_INT((int)player->s.appearance,
                  (int)Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0));
    ASSERT_EQ_INT((int)player->s.equipment,
                  (int)Wow_PackEquipment(1, 1, 1, 1));

    if (game->Shutdown) {
        game->Shutdown();
    }
}

static void test_wow_load_map_spawns_and_runs_creature_state(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *player_local;
    VECTOR2 before;
    LPCSTR attack_argv[] = { "attack", "1" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    ASSERT_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    player_local = Wow_EntityLocal(player);
    before = creature->s.origin2;

    ASSERT_EQ_INT((int)creature->s.number, 1);
    ASSERT_EQ_INT((int)creature_local->kind, WOW_ENTITY_CREATURE);
    ASSERT_EQ_INT((int)creature_local->display_id, 161);
    ASSERT_EQ_INT((int)creature_local->health, 3);
    ASSERT_EQ_INT((int)creature_local->max_health, BZ_WOW_CREATURE_BASE_HEALTH);
    ASSERT_EQ_INT((int)creature_local->level, 1);
    ASSERT_EQ_INT((int)creature_local->xp_reward, BZ_WOW_CREATURE_KILL_XP);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_IDLE);
    ASSERT_EQ_INT((int)creature->s.stats[ENT_HEALTH], 255);
    ASSERT((creature->svflags & SVF_MONSTER) != 0);
    ASSERT((creature->s.flags & EF_GROUND_ANCHOR) != 0);
    ASSERT_EQ_INT((int)creature->s.player, 2);
    ASSERT_EQ_FLOAT(creature->s.scale, 1.0f, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.radius, 1.5f, 0.001f);
    ASSERT_NOT_NULL(creature_local->animation);
    ASSERT_STR_EQ(creature_local->animation->name, "Walk");

    game->RunFrame();
    ASSERT(fabsf(creature->s.origin2.x - before.x) > 0.001f ||
           fabsf(creature->s.origin2.y - before.y) > 0.001f);

    game->ClientCommand(player, 2, attack_argv);
    ASSERT_EQ_INT((int)(player_local->enemy ? player_local->enemy->s.number : 0), 1);
    ASSERT_EQ_INT((int)player->client->ps.selected_entity, 1);

    /* Run frames until the player chases into melee range and starts swinging. */
    for (int i = 0; i < 300; i++) {
        game->RunFrame();
        if (player_local->attack_damage_time > 0) break;
    }
    ASSERT(player_local->attack_damage_time > 0);
    ASSERT(player_local->attack_backswing_time > 0);
    ASSERT_NOT_NULL(player_local->animation);
    ASSERT_STR_EQ(player_local->animation->name, "Attack1H");
    ASSERT(creature_local->enemy == player);
    ASSERT(creature_local->ai_state == WOW_AI_CHASE || creature_local->ai_state == WOW_AI_ATTACK);

    if (game->Shutdown) {
        game->Shutdown();
    }
}

/* Number-key action slots enter the existing melee/projectile runtime and publish server cooldown state. */
static void test_wow_action_commands_bind_three_server_abilities(void) {
    struct game_export *game = init_game();
    LPEDICT player, creature;
    wowEntityLocal_t *player_local, *creature_local;
    wowClient_t *client;
    LPCSTR select_argv[] = { "select", "1" };
    LPCSTR strike_argv[] = { "wow_action", "0" };
    LPCSTR heavy_argv[] = { "wow_action", "1" };
    LPCSTR throw_argv[] = { "wow_action", "2" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    ASSERT_NOT_NULL(creature);
    player_local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    client = (wowClient_t *)player->client;
    creature->s.origin = (VECTOR3){ player->s.origin.x + 4.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (VECTOR2){ creature->s.origin.x, creature->s.origin.y };
    creature_local->health = creature_local->max_health = 10;
    game->ClientBegin(player);
    game->ClientCommand(player, 2, select_argv);

    game->ClientCommand(player, 2, strike_argv);
    ASSERT_EQ_INT((int)player_local->attack_damage, BZ_WOW_STRIKE_DAMAGE);
    ASSERT(player_local->attack_damage_time > 0);
    player_local->attack_time = player_local->attack_damage_time = player_local->attack_backswing_time = 0;
    player_local->attack_damage_done = false;

    player_local->power = BZ_WOW_HEAVY_STRIKE_RAGE;
    game->ClientCommand(player, 2, heavy_argv);
    ASSERT_EQ_INT((int)player_local->attack_damage, BZ_WOW_HEAVY_STRIKE_DAMAGE);
    ASSERT_EQ_INT((int)player_local->power, 0);
    ASSERT_EQ_INT((int)player_local->ability_cooldown[WOW_ABILITY_HEAVY_STRIKE],
                  BZ_WOW_HEAVY_STRIKE_COOLDOWN);
    player_local->attack_time = player_local->attack_damage_time = player_local->attack_backswing_time = 0;
    player_local->attack_damage_done = false;

    game->ClientCommand(player, 2, throw_argv);
    ASSERT_EQ_INT((int)Wow_EntityLocal(&wow_edicts[globals.num_edicts - 1])->kind, WOW_ENTITY_PROJECTILE);
    ASSERT_EQ_INT((int)player_local->ability_cooldown[WOW_ABILITY_THROW], BZ_WOW_THROW_COOLDOWN);
    game->RunFrame();
    ASSERT_STR_EQ(client->actions[0].name, "Strike");
    ASSERT_STR_EQ(client->actions[1].name, "Heavy Strike");
    ASSERT_STR_EQ(client->actions[2].name, "Throw");
    ASSERT_EQ_INT((int)client->actions[1].rage_cost, BZ_WOW_HEAVY_STRIKE_RAGE);
    ASSERT_EQ_INT((int)client->actions[1].cooldown, 2);
    ASSERT_EQ_INT((int)client->actions[2].cooldown, 2);
    ASSERT(client->actions[1].flags & WOW_ACTION_DISABLED_COOLDOWN);
    ASSERT(client->actions[2].flags & WOW_ACTION_DISABLED_COOLDOWN);

    if (game->Shutdown) game->Shutdown();
}

/* The controlled neutral world creature is selectable and exposes the same quest functions used by HUD commands. */
static void test_wow_first_quest_acceptance_requires_its_world_giver(void) {
    struct game_export *game = init_game();
    LPEDICT player, giver, wrong_giver;
    wowEntityLocal_t *giver_local;
    wowClient_t *client;
    char giver_number[16];
    LPCSTR select_argv[] = { "select", giver_number };
    LPCSTR accept_argv[] = { "quest_accept", "first_hunt" };
    LPCSTR turn_in_argv[] = { "quest_turn_in", "first_hunt" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    giver = quest_giver();
    wrong_giver = first_creature();
    ASSERT_NOT_NULL(giver);
    ASSERT_NOT_NULL(wrong_giver);
    giver_local = Wow_EntityLocal(giver);
    client = (wowClient_t *)player->client;
    ASSERT_NOT_NULL(Wow_QuestDef(WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)Wow_QuestId("first_hunt"), WOW_QUEST_FIRST_HUNT);
    ASSERT_EQ_INT((int)Wow_QuestId("missing"), WOW_QUEST_NONE);
    ASSERT_EQ_INT((int)Wow_QuestDef(WOW_QUEST_FIRST_HUNT)->required_count, 4);
    ASSERT_EQ_INT((int)Wow_QuestDef(WOW_QUEST_FIRST_HUNT)->xp_reward, 250);
    ASSERT_EQ_INT((int)Wow_QuestDef(WOW_QUEST_FIRST_HUNT)->item_reward.item,
                  WOW_ITEM_MINOR_HEALING_POTION);
    ASSERT_EQ_INT((int)giver->s.number, 5);
    ASSERT_EQ_INT((int)giver_local->quest_giver, WOW_QUEST_FIRST_HUNT);
    ASSERT(!giver_local->hostile);
    ASSERT(!(giver->s.renderfx & RF_HOSTILE));
    ASSERT(Wow_EntityCanBeSelected(giver));
    ASSERT(!Wow_EntityCanBeTargeted(giver));
    Wow_DealDamage(giver, player, giver_local->health);
    ASSERT_EQ_INT((int)giver_local->health, (int)giver_local->max_health);
    ASSERT(!Wow_AcceptQuest(player, wrong_giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_AVAILABLE);
    snprintf(giver_number, sizeof(giver_number), "%u", (unsigned)giver->s.number);
    game->ClientBegin(player);
    game->ClientCommand(player, 2, select_argv);
    game->RunFrame();
    ASSERT_EQ_INT((int)player->client->ps.selected_entity, (int)giver->s.number);
    ASSERT_STR_EQ(client->quest_hud.action_command, "quest_accept first_hunt");

    game->ClientCommand(player, 2, accept_argv);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);
    ASSERT_EQ_INT((int)client->quest.count, 0);
    ASSERT_EQ_INT((int)Wow_EntityLocal(player)->xp, 0);
    game->ClientCommand(player, 2, accept_argv);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);
    game->ClientCommand(player, 2, turn_in_argv);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);
    ASSERT(!Wow_TurnInQuest(player, wrong_giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);

    if (game->Shutdown) game->Shutdown();
}

/* Only the existing guarded death transition can advance melee/projectile kill progress, once per living spawn. */
static void test_wow_first_quest_counts_confirmed_kills_once(void) {
    struct game_export *game = init_game();
    LPEDICT player, giver, creatures[8], projectile;
    wowClient_t *client;
    wowEntityLocal_t *projectile_local;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    giver = quest_giver();
    client = (wowClient_t *)player->client;
    ASSERT(hostile_creatures(creatures, 8) >= 8);
    ASSERT(Wow_AcceptQuest(player, giver, WOW_QUEST_FIRST_HUNT));

    Wow_AIDie(giver, player);
    ASSERT_EQ_INT((int)client->quest.count, 0);
    Wow_AIDie(creatures[0], player);
    ASSERT_EQ_INT((int)client->quest.count, 1);
    Wow_AIDie(creatures[0], player);
    ASSERT_EQ_INT((int)client->quest.count, 1);
    projectile = Wow_Spawn();
    ASSERT_NOT_NULL(projectile);
    projectile->inuse = true;
    projectile_local = Wow_EntityLocal(projectile);
    projectile_local->kind = WOW_ENTITY_PROJECTILE;
    projectile_local->projectile_caster = player->s.number;
    Wow_AIDie(creatures[1], projectile);
    ASSERT_EQ_INT((int)client->quest.count, 2);
    Wow_AIDie(creatures[2], creatures[3]);
    ASSERT_EQ_INT((int)client->quest.count, 2);
    Wow_EntityLocal(creatures[4])->ai_state = WOW_AI_EVADE;
    Wow_AIRunFrame(creatures[4]);
    ASSERT_EQ_INT((int)client->quest.count, 2);
    Wow_AIDie(creatures[4], player);
    Wow_AIDie(creatures[5], player);
    ASSERT_EQ_INT((int)client->quest.count, 4);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_READY_TO_TURN_IN);
    Wow_AIDie(creatures[6], player);
    ASSERT_EQ_INT((int)client->quest.count, 4);

    if (game->Shutdown) game->Shutdown();
}

static void test_wow_first_quest_respawn_counts_only_a_new_death(void) {
    struct game_export *game = init_game();
    LPEDICT player, giver, creatures[1];
    wowClient_t *client;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    giver = quest_giver();
    client = (wowClient_t *)player->client;
    ASSERT_EQ_INT((int)hostile_creatures(creatures, 1), 1);
    ASSERT(Wow_AcceptQuest(player, giver, WOW_QUEST_FIRST_HUNT));
    Wow_AIDie(creatures[0], player);
    ASSERT_EQ_INT((int)client->quest.count, 1);
    Wow_AIDie(creatures[0], player);
    ASSERT_EQ_INT((int)client->quest.count, 1);
    FOR_LOOP(i, 70) Wow_AIRunFrame(creatures[0]);
    ASSERT(!Wow_EntityLocal(creatures[0])->dead);
    Wow_AIDie(creatures[0], player);
    ASSERT_EQ_INT((int)client->quest.count, 2);

    if (game->Shutdown) game->Shutdown();
}

/* Turn-in first inserts the complete item reward, so a full bag cannot partially grant XP or completion. */
static void test_wow_first_quest_turn_in_is_atomic_and_once(void) {
    struct game_export *game = init_game();
    LPEDICT player, giver;
    wowEntityLocal_t *player_local;
    wowClient_t *client;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    giver = quest_giver();
    player_local = Wow_EntityLocal(player);
    client = (wowClient_t *)player->client;
    client->quest.state = WOW_QUEST_READY_TO_TURN_IN;
    client->quest.count = Wow_QuestDef(WOW_QUEST_FIRST_HUNT)->required_count;
    player_local->xp = 300;
    ASSERT(Wow_GiveItem(player, WOW_ITEM_TRAINING_SWORD, WOW_UI_INVENTORY_SLOTS));
    ASSERT(!Wow_TurnInQuest(player, giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_READY_TO_TURN_IN);
    ASSERT_EQ_INT((int)player_local->xp, 300);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 0);
    ASSERT_EQ_INT((int)client->combat_message.type, WOW_COMBAT_MESSAGE_INVENTORY_FULL);

    memset(&client->bag[WOW_UI_INVENTORY_SLOTS - 1], 0, sizeof(client->bag[0]));
    ASSERT(Wow_TurnInQuest(player, giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_COMPLETED);
    ASSERT_EQ_INT((int)player_local->level, 2);
    ASSERT_EQ_INT((int)player_local->xp, 150);
    ASSERT_EQ_INT((int)player_local->max_health, 110);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 1);
    ASSERT(!Wow_TurnInQuest(player, giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)player_local->xp, 150);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 1);

    if (game->Shutdown) game->Shutdown();
}

/* Native HUD commands loot one nearby corpse and equip the resulting server-owned items. */
static void test_wow_loot_inventory_and_equipment_command_loop(void) {
    struct game_export *game = init_game();
    LPEDICT player, creature;
    wowEntityLocal_t *creature_local;
    wowClient_t *client;
    LPCSTR loot_argv[] = { "loot", "1" };
    LPCSTR sword_argv[] = { "use_item", "1" };
    LPCSTR armor_argv[] = { "use_item", "2" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    ASSERT_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    client = (wowClient_t *)player->client;
    creature->s.origin = (VECTOR3){ player->s.origin.x + 4.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (VECTOR2){ creature->s.origin.x, creature->s.origin.y };
    game->ClientBegin(player);
    Wow_AIDie(creature, player);
    game->RunFrame();
    ASSERT_EQ_INT((int)client->loot_target, (int)creature->s.number);

    game->ClientCommand(player, 2, loot_argv);
    ASSERT_EQ_INT((int)creature_local->loot_state, WOW_LOOT_PICKED);
    ASSERT_EQ_INT((int)client->bag[0].item, WOW_ITEM_MINOR_HEALING_POTION);
    ASSERT_EQ_INT((int)client->bag[1].item, WOW_ITEM_TRAINING_SWORD);
    ASSERT_EQ_INT((int)client->bag[2].item, WOW_ITEM_PADDED_ARMOR);
    game->ClientCommand(player, 2, sword_argv);
    game->ClientCommand(player, 2, armor_argv);
    game->RunFrame();
    ASSERT_EQ_INT((int)client->equipment[WOW_EQUIPMENT_WEAPON], WOW_ITEM_TRAINING_SWORD);
    ASSERT_EQ_INT((int)client->equipment[WOW_EQUIPMENT_ARMOR], WOW_ITEM_PADDED_ARMOR);
    ASSERT_STR_EQ(client->inventory[0].name, "Minor Healing Potion");
    ASSERT_STR_EQ(client->inventory[1].name, "Training Sword");
    ASSERT_STR_EQ(client->inventory[2].name, "Padded Armor");
    ASSERT_STR_EQ(client->equipment_text, "Weapon: Training Sword | Armor: Padded Armor");
    ASSERT_EQ_INT((int)client->loot_target, 0);

    if (game->Shutdown) game->Shutdown();
}

/* GM exploration speeds manual travel but never bypasses mode, input, or world-bound validation. */
static void test_wow_gm_mode_accelerates_and_guards_teleport(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    VECTOR3 before;
    FLOAT normal_step;
    LPCSTR move_argv[] = { "move", "1", "0", "328", "8.5" };
    LPCSTR gm_on_argv[] = { "gm", "on" };
    LPCSTR gm_off_argv[] = { "gm", "off" };
    LPCSTR gm_invalid_argv[] = { "gm", "invalid" };
    LPCSTR gm_toggle_argv[] = { "gm" };
    LPCSTR teleport_argv[] = { "teleport", "100", "200" };
    LPCSTR teleport_short_argv[] = { "teleport", "100" };
    LPCSTR teleport_bad_argv[] = { "teleport", "bad", "200" };
    LPCSTR teleport_nan_argv[] = { "teleport", "nan", "200" };
    LPCSTR teleport_oob_argv[] = { "teleport", "40000", "200" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientCommand(player, 5, move_argv);
    before = player->s.origin;
    game->RunFrame();
    normal_step = player->s.origin.x - before.x;
    ASSERT_EQ_FLOAT(normal_step, WOW_WALK_SPEED * FRAMETIME / 1000.0f, 0.001f);

    game->ClientCommand(player, 2, gm_on_argv);
    before = player->s.origin;
    game->RunFrame();
    ASSERT_EQ_FLOAT(player->s.origin.x - before.x, BZ_WOW_GM_SPEED * FRAMETIME / 1000.0f, 0.001f);

    game->ClientCommand(player, 2, gm_invalid_argv);
    before = player->s.origin;
    game->RunFrame();
    ASSERT_EQ_FLOAT(player->s.origin.x - before.x, BZ_WOW_GM_SPEED * FRAMETIME / 1000.0f, 0.001f);

    game->ClientCommand(player, 1, gm_toggle_argv);
    before = player->s.origin;
    game->RunFrame();
    ASSERT_EQ_FLOAT(player->s.origin.x - before.x, normal_step, 0.001f);
    game->ClientCommand(player, 2, gm_on_argv);
    game->ClientCommand(player, 2, gm_off_argv);
    before = player->s.origin;
    game->RunFrame();
    ASSERT_EQ_FLOAT(player->s.origin.x - before.x, normal_step, 0.001f);
    before = player->s.origin;
    game->ClientCommand(player, 3, teleport_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, before.x, 0.001f);
    ASSERT_EQ_FLOAT(player->s.origin.y, before.y, 0.001f);

    game->ClientCommand(player, 2, gm_on_argv);
    game->ClientCommand(player, 2, teleport_short_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, before.x, 0.001f);
    game->ClientCommand(player, 3, teleport_bad_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, before.x, 0.001f);
    game->ClientCommand(player, 3, teleport_nan_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, before.x, 0.001f);
    game->ClientCommand(player, 3, teleport_oob_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, before.x, 0.001f);
    game->ClientCommand(player, 3, teleport_argv);
    ASSERT_EQ_FLOAT(player->s.origin.x, 100.0f, 0.001f);
    ASSERT_EQ_FLOAT(player->s.origin.y, 200.0f, 0.001f);
    ASSERT_EQ_FLOAT(player->s.origin.z, CM_GetHeightAtPoint(100.0f, 200.0f), 0.001f);

    if (game->Shutdown)
        game->Shutdown();
}

/* Quest-log visibility is server-owned and refreshes only its dedicated layout layer. */
static void test_wow_quest_log_commands_toggle_server_ui(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    LPCSTR open_argv[] = { "questlog", "open" };
    LPCSTR close_argv[] = { "questlog", "close" };
    LPCSTR toggle_argv[] = { "questlog", "toggle" };
    LPCSTR invalid_argv[] = { "questlog", "invalid" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    wc = (wowClient_t *)player->client;
    ASSERT(!wc->quest_log_open);

    game->ClientCommand(player, 2, open_argv);
    ASSERT(wc->quest_log_open);
    ASSERT_EQ_INT((int)test_quest_show_calls, 1);
    ASSERT_EQ_INT((int)test_quest_hide_calls, 0);

    game->ClientCommand(player, 2, invalid_argv);
    ASSERT(wc->quest_log_open);
    ASSERT_EQ_INT((int)test_quest_show_calls, 1);
    ASSERT_EQ_INT((int)test_quest_hide_calls, 0);

    game->ClientCommand(player, 2, toggle_argv);
    ASSERT(!wc->quest_log_open);
    ASSERT_EQ_INT((int)test_quest_hide_calls, 1);
    game->ClientCommand(player, 2, close_argv);
    ASSERT(!wc->quest_log_open);
    ASSERT_EQ_INT((int)test_quest_hide_calls, 2);

    if (game->Shutdown) game->Shutdown();
}

/* MCNK area IDs resolve through the artificial AreaTable and refresh the server-authored header once. */
static void test_wow_zone_name_tracks_player_area(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    DWORD initial_hud_writes;
    LPCSTR gm_on_argv[] = { "gm", "on" };
    LPCSTR teleport_argv[] = { "teleport", "100", "200" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    wc = (wowClient_t *)player->client;
    ASSERT_STR_EQ(CM_WowAreaNameAtPoint(100.0f, 200.0f), "Test Valley");
    game->ClientBegin(player);
    initial_hud_writes = test_hud_write_calls;
    game->ClientCommand(player, 2, gm_on_argv);
    game->ClientCommand(player, 3, teleport_argv);
    game->RunFrame();
    ASSERT_STR_EQ(wc->zone_name, "Test Valley");
    ASSERT_EQ_INT((int)test_hud_write_calls, (int)initial_hud_writes + 1);
    game->RunFrame();
    ASSERT_EQ_INT((int)test_hud_write_calls, (int)initial_hud_writes + 1);

    if (game->Shutdown) game->Shutdown();
}

/* Vitals are copied from the server entity and rebuild the profile only when a displayed value changes. */
static void test_wow_player_progression_refreshes_live_hud_once(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowEntityLocal_t *local;
    DWORD initial_hud_writes;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);
    game->ClientBegin(player);
    initial_hud_writes = test_hud_write_calls;
    local->health = 75;
    local->power = 20;
    local->xp = 100;
    game->RunFrame();
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH], 75);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_POWER], 20);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_XP], 100);
    ASSERT_EQ_INT((int)test_hud_write_calls, (int)initial_hud_writes + 1);
    game->RunFrame();
    ASSERT_EQ_INT((int)test_hud_write_calls, (int)initial_hud_writes + 1);

    local->level = 2;
    local->health = local->max_health = 110;
    local->xp = 50;
    game->RunFrame();
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_LEVEL], 2);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH_MAX], 110);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_XP_MAX], 900);
    ASSERT_EQ_INT((int)test_hud_write_calls, (int)initial_hud_writes + 2);

    if (game->Shutdown) game->Shutdown();
}

static void test_wow_progress_missing_save_and_confirmed_reset_use_defaults(void) {
    struct game_export *game = init_game();
    LPCSTR save_argv[] = { "save_progress" };
    LPCSTR reset_argv[] = { "reset_progress" };
    LPCSTR confirm_argv[] = { "reset_progress", "confirm" };
    LPEDICT player;
    wowEntityLocal_t *local;
    wowClient_t *client;

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);
    client = (wowClient_t *)player->client;
    ASSERT_EQ_INT((int)local->level, 1);
    ASSERT_EQ_INT((int)local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    ASSERT_EQ_INT(access(TEST_PROGRESS_PATH, F_OK), -1);

    local->level = 2;
    local->xp = 75;
    local->health = local->max_health = 110;
    client->quest.state = WOW_QUEST_ACTIVE;
    client->quest.count = 2;
    game->ClientCommand(player, 1, save_argv);
    ASSERT_EQ_INT(access(TEST_PROGRESS_PATH, F_OK), 0);
    ASSERT_EQ_INT((int)client->combat_message.type, WOW_COMBAT_MESSAGE_PROGRESS_SAVED);
    ASSERT_STR_EQ(client->combat_message.text, "Progress saved");

    game->ClientCommand(player, 1, reset_argv);
    ASSERT_EQ_INT((int)local->level, 2);
    ASSERT_EQ_INT((int)client->quest.count, 2);
    game->ClientCommand(player, 2, confirm_argv);
    ASSERT_EQ_INT((int)local->level, 1);
    ASSERT_EQ_INT((int)local->xp, 0);
    ASSERT_EQ_INT((int)local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_AVAILABLE);
    ASSERT_EQ_INT((int)client->quest.count, 0);
    ASSERT_EQ_INT((int)client->combat_message.type, WOW_COMBAT_MESSAGE_PROGRESS_RESET);
    ASSERT_STR_EQ(client->combat_message.text, "Progress reset");

    local->level = 2;
    local->max_health = 110;
    local->health = 1;
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(player), WOW_PROGRESS_LOAD_OK);
    ASSERT_EQ_INT((int)local->level, 1);
    ASSERT_EQ_INT((int)local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

static void test_wow_progress_round_trip_restores_rpg_state_not_transients(void) {
    struct game_export *game = init_game();
    LPEDICT player, creature;
    wowEntityLocal_t *local, *creature_local;
    wowClient_t *client;

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    creature_local = Wow_EntityLocal(creature);
    client = (wowClient_t *)player->client;
    local->level = 2;
    local->xp = 123;
    local->health = 77;
    local->max_health = 110;
    local->power = 42;
    ASSERT(Wow_GiveItem(player, WOW_ITEM_MINOR_HEALING_POTION, 4));
    ASSERT(Wow_GiveItem(player, WOW_ITEM_TRAINING_SWORD, 1));
    ASSERT(Wow_GiveItem(player, WOW_ITEM_PADDED_ARMOR, 1));
    ASSERT(Wow_UseInventorySlot(player, 1));
    ASSERT(Wow_UseInventorySlot(player, 2));
    client->quest.state = WOW_QUEST_ACTIVE;
    client->quest.count = 2;
    local->enemy = creature;
    local->ability_cooldown[WOW_ABILITY_THROW] = 900;
    creature_local->ai_state = WOW_AI_AGGRO;
    creature_local->health = 1;
    ASSERT(Wow_SavePlayerProgress(player));
    ASSERT_EQ_INT(access(TEST_PROGRESS_PATH ".tmp", F_OK), -1);

    Wow_ResetPlayerProgress(player);
    creature_local->ai_state = WOW_AI_EVADE;
    creature_local->health = 2;
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(player), WOW_PROGRESS_LOAD_OK);
    ASSERT_EQ_INT((int)local->level, 2);
    ASSERT_EQ_INT((int)local->xp, 123);
    ASSERT_EQ_INT((int)local->health, 77);
    ASSERT_EQ_INT((int)local->max_health, 110);
    ASSERT_EQ_INT((int)local->power, 42);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 4);
    ASSERT_EQ_INT((int)client->equipment[WOW_EQUIPMENT_WEAPON], WOW_ITEM_TRAINING_SWORD);
    ASSERT_EQ_INT((int)client->equipment[WOW_EQUIPMENT_ARMOR], WOW_ITEM_PADDED_ARMOR);
    ASSERT_EQ_INT((int)Wow_PlayerWeaponBonus(player), 1);
    ASSERT_EQ_INT((int)Wow_AdjustIncomingDamage(player, 3), 2);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);
    ASSERT_EQ_INT((int)client->quest.count, 2);
    ASSERT_NULL(local->enemy);
    ASSERT_EQ_INT((int)local->ability_cooldown[WOW_ABILITY_THROW], 0);
    ASSERT_EQ_INT((int)creature_local->ai_state, WOW_AI_EVADE);
    ASSERT_EQ_INT((int)creature_local->health, 2);

    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

static void test_wow_progress_load_clamps_health_and_rage(void) {
    static LPCSTR const over_limit =
        "OPENWOW_PROGRESS 1\n"
        "player 2 50\n"
        "vitals 999 110 999\n"
        "bag 6\n"
        "slot 0 0 0\nslot 1 0 0\nslot 2 0 0\n"
        "slot 3 0 0\nslot 4 0 0\nslot 5 0 0\n"
        "equipment 0 0\n"
        "quest 1 2 2\n"
        "end\n";
    struct game_export *game = init_game();
    wowEntityLocal_t *local;

    test_progress_path_enable();
    test_write_local_file(TEST_PROGRESS_PATH, over_limit);
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    ASSERT_EQ_INT((int)local->level, 2);
    ASSERT_EQ_INT((int)local->xp, 50);
    ASSERT_EQ_INT((int)local->max_health, 110);
    ASSERT_EQ_INT((int)local->health, 110);
    ASSERT_EQ_INT((int)local->power, BZ_WOW_PLAYER_MAX_POWER);
    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

static void test_wow_progress_rejects_corrupt_unknown_items_and_future_versions(void) {
    static LPCSTR const unknown_item =
        "OPENWOW_PROGRESS 1\n"
        "player 1 0\n"
        "vitals 100 100 0\n"
        "bag 6\n"
        "slot 0 99 1\nslot 1 0 0\nslot 2 0 0\n"
        "slot 3 0 0\nslot 4 0 0\nslot 5 0 0\n"
        "equipment 0 0\n"
        "quest 1 1 0\n"
        "end\n";
    struct game_export *game = init_game();
    char text[512] = { 0 };
    wowEntityLocal_t *local;

    test_progress_path_enable();
    test_write_local_file(TEST_PROGRESS_PATH, "broken save\n");
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    ASSERT_EQ_INT((int)local->level, 1);
    ASSERT_EQ_INT((int)local->health, BZ_WOW_PLAYER_BASE_HEALTH);
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(&wow_edicts[0]), WOW_PROGRESS_LOAD_INVALID);
    test_write_local_file(TEST_PROGRESS_PATH, unknown_item);
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(&wow_edicts[0]), WOW_PROGRESS_LOAD_INVALID);
    if (game->Shutdown) game->Shutdown();
    test_read_local_file(TEST_PROGRESS_PATH, text, sizeof(text));
    ASSERT_STR_EQ(text, unknown_item);
    test_progress_paths_clear();

    game = init_game();
    test_progress_path_enable();
    test_write_local_file(TEST_PROGRESS_PATH, "OPENWOW_PROGRESS 2\n");
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(&wow_edicts[0]), WOW_PROGRESS_LOAD_UNSUPPORTED);
    if (game->Shutdown) game->Shutdown();
    memset(text, 0, sizeof(text));
    test_read_local_file(TEST_PROGRESS_PATH, text, sizeof(text));
    ASSERT_STR_EQ(text, "OPENWOW_PROGRESS 2\n");
    test_progress_paths_clear();
}

static void test_wow_progress_completed_quest_cannot_reward_again_after_load(void) {
    struct game_export *game = init_game();
    LPEDICT player, giver;
    wowEntityLocal_t *local;
    wowClient_t *client;

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    giver = quest_giver();
    local = Wow_EntityLocal(player);
    client = (wowClient_t *)player->client;
    local->xp = 150;
    ASSERT(Wow_GiveItem(player, WOW_ITEM_MINOR_HEALING_POTION, 1));
    client->quest.state = WOW_QUEST_COMPLETED;
    client->quest.count = Wow_QuestDef(WOW_QUEST_FIRST_HUNT)->required_count;
    ASSERT(Wow_SavePlayerProgress(player));
    Wow_ResetPlayerProgress(player);
    ASSERT_EQ_INT((int)Wow_LoadPlayerProgress(player), WOW_PROGRESS_LOAD_OK);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_COMPLETED);
    ASSERT_EQ_INT((int)client->quest.count, 4);
    ASSERT(!Wow_TurnInQuest(player, giver, WOW_QUEST_FIRST_HUNT));
    ASSERT_EQ_INT((int)local->xp, 150);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 1);

    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

static void test_wow_progress_atomic_save_preserves_target_when_temp_write_fails(void) {
    struct game_export *game = init_game();
    char temporary[MAX_PATHLEN];
    char text[64] = { 0 };

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_write_local_file(TEST_PROGRESS_PATH, "original snapshot\n");
    snprintf(temporary, sizeof(temporary), "%s.tmp", TEST_PROGRESS_PATH);
    ASSERT_EQ_INT(mkdir(temporary, 0700), 0);
    ASSERT(!Wow_SavePlayerProgress(&wow_edicts[0]));
    test_read_local_file(TEST_PROGRESS_PATH, text, sizeof(text));
    ASSERT_STR_EQ(text, "original snapshot\n");

    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    ASSERT_EQ_INT(rmdir(temporary), 0);
    test_progress_paths_clear();
}

static void test_wow_progress_regular_shutdown_autosaves_for_next_start(void) {
    struct game_export *game = init_game();
    wowEntityLocal_t *local;
    wowClient_t *client;

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    client = (wowClient_t *)wow_edicts[0].client;
    local->level = 3;
    local->xp = 75;
    local->health = 66;
    local->max_health = 120;
    local->power = 33;
    ASSERT(Wow_GiveItem(&wow_edicts[0], WOW_ITEM_MINOR_HEALING_POTION, 5));
    client->quest.state = WOW_QUEST_READY_TO_TURN_IN;
    client->quest.count = 4;
    if (game->Shutdown) game->Shutdown();
    ASSERT_EQ_INT(access(TEST_PROGRESS_PATH, F_OK), 0);

    game = init_game();
    snprintf(test_save_path, sizeof(test_save_path), "%s", TEST_PROGRESS_PATH);
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    client = (wowClient_t *)wow_edicts[0].client;
    ASSERT_EQ_INT((int)local->level, 3);
    ASSERT_EQ_INT((int)local->xp, 75);
    ASSERT_EQ_INT((int)local->health, 66);
    ASSERT_EQ_INT((int)local->max_health, 120);
    ASSERT_EQ_INT((int)local->power, 33);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 5);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_READY_TO_TURN_IN);
    ASSERT_EQ_INT((int)client->quest.count, 4);
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

static void test_wow_progress_map_transition_preserves_current_snapshot(void) {
    struct game_export *game = init_game();
    wowEntityLocal_t *local;
    wowClient_t *client;

    test_progress_path_enable();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    client = (wowClient_t *)wow_edicts[0].client;
    local->level = 2;
    local->xp = 91;
    local->health = 80;
    local->max_health = 110;
    local->power = 17;
    ASSERT(Wow_GiveItem(&wow_edicts[0], WOW_ITEM_MINOR_HEALING_POTION, 3));
    client->quest.state = WOW_QUEST_ACTIVE;
    client->quest.count = 1;
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(&wow_edicts[0]);
    client = (wowClient_t *)wow_edicts[0].client;
    ASSERT_EQ_INT((int)local->level, 2);
    ASSERT_EQ_INT((int)local->xp, 91);
    ASSERT_EQ_INT((int)local->health, 80);
    ASSERT_EQ_INT((int)local->max_health, 110);
    ASSERT_EQ_INT((int)local->power, 17);
    ASSERT_EQ_INT((int)test_inventory_count(client, WOW_ITEM_MINOR_HEALING_POTION), 3);
    ASSERT_EQ_INT((int)client->quest.state, WOW_QUEST_ACTIVE);
    ASSERT_EQ_INT((int)client->quest.count, 1);

    test_save_path[0] = '\0';
    if (game->Shutdown) game->Shutdown();
    test_progress_paths_clear();
}

int main(void) {
    RUN_TEST(test_wow_load_map_initializes_player_state);
    RUN_TEST(test_wow_load_map_spawns_and_runs_creature_state);
    RUN_TEST(test_wow_action_commands_bind_three_server_abilities);
    RUN_TEST(test_wow_first_quest_acceptance_requires_its_world_giver);
    RUN_TEST(test_wow_first_quest_counts_confirmed_kills_once);
    RUN_TEST(test_wow_first_quest_respawn_counts_only_a_new_death);
    RUN_TEST(test_wow_first_quest_turn_in_is_atomic_and_once);
    RUN_TEST(test_wow_loot_inventory_and_equipment_command_loop);
    RUN_TEST(test_wow_gm_mode_accelerates_and_guards_teleport);
    RUN_TEST(test_wow_quest_log_commands_toggle_server_ui);
    RUN_TEST(test_wow_zone_name_tracks_player_area);
    RUN_TEST(test_wow_player_progression_refreshes_live_hud_once);
    RUN_TEST(test_wow_progress_missing_save_and_confirmed_reset_use_defaults);
    RUN_TEST(test_wow_progress_round_trip_restores_rpg_state_not_transients);
    RUN_TEST(test_wow_progress_load_clamps_health_and_rage);
    RUN_TEST(test_wow_progress_rejects_corrupt_unknown_items_and_future_versions);
    RUN_TEST(test_wow_progress_completed_quest_cannot_reward_again_after_load);
    RUN_TEST(test_wow_progress_atomic_save_preserves_target_when_temp_write_fails);
    RUN_TEST(test_wow_progress_regular_shutdown_autosaves_for_next_start);
    RUN_TEST(test_wow_progress_map_transition_preserves_current_snapshot);
    TEST_RESULTS();
}
