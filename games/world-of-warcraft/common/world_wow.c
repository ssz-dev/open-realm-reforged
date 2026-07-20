#include "common/common.h"
#include "common/wow_world_query.h"
#include <limits.h>
#include <math.h>

#define CM_WOW_ADT_SIZE       533.333313f
#define CM_WOW_ADT_UNIT_SIZE  (CM_WOW_ADT_SIZE / 16.0f / 8.0f)
#define CM_WOW_ADT_CHUNK_SIZE (CM_WOW_ADT_SIZE / 16.0f)
#define CM_WOW_MCVT_COUNT     (9 * 9 + 8 * 8)
#define CM_WOW_ADT_CACHE_SIZE 9

typedef struct {
    BOOL    has_heights;
    DWORD   area_id;
    VECTOR3 position;
    float   heights[CM_WOW_MCVT_COUNT];
} cmWowChunkHeight_t;

typedef struct {
    BOOL              loaded;
    BOOL              valid;
    int               tile_x;
    int               tile_y;
    DWORD             stamp;
    cmWowChunkHeight_t chunks[16][16];
} cmWowAdtHeightCache_t;

typedef struct {
    DWORD id;
    DWORD map_id;
    VECTOR3 position;
} cmWowWorldSafeLoc_t;

static VECTOR3              cm_wow_spawn_position = { 0.0f, 0.0f, 0.0f };
static FLOAT                cm_wow_spawn_heights[MAX_PLAYERS];
static char                 cm_wow_map_dir[PATH_MAX]  = { 0 };
static char                 cm_wow_map_name[128]      = { 0 };
static cmWowAdtHeightCache_t cm_wow_height_cache[CM_WOW_ADT_CACHE_SIZE];
static DWORD                cm_wow_height_cache_stamp;

static DWORD CM_WowRead32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static FLOAT CM_WowReadFloat(BYTE const *p) {
    FLOAT value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static BOOL CM_WowTagEquals(BYTE const *tag, LPCSTR reversed) {
    return memcmp(tag, reversed, 4) == 0;
}

static LPCSTR CM_WowDbcString(BYTE const *string_block, DWORD string_size, DWORD offset) {
    if (offset >= string_size)
        return NULL;
    return (LPCSTR)(string_block + offset);
}

static LPSTR CM_WowCopyString(LPCSTR value) {
    size_t len;
    LPSTR out;

    if (!value || !*value)
        return NULL;
    len = strlen(value);
    out = MemAlloc((long)len + 1);
    memcpy(out, value, len + 1);
    return out;
}

static BOOL CM_WowExtractMapName(LPCSTR mapFilename, LPSTR out, size_t out_size) {
    LPCSTR start, slash, backslash, dot;
    size_t len;

    if (!mapFilename || !out || out_size == 0)
        return false;

    slash     = strrchr(mapFilename, '/');
    backslash = strrchr(mapFilename, '\\');
    start = slash && backslash ? MAX(slash, backslash) + 1
          : slash              ? slash + 1
          : backslash          ? backslash + 1
          :                      mapFilename;
    dot = strrchr(start, '.');
    len = dot && dot > start ? (size_t)(dot - start) : strlen(start);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len > 0;
}

static void CM_WowSetMapPath(LPCSTR mapFilename) {
    LPCSTR path  = mapFilename && *mapFilename ? mapFilename : "World/Maps/Azeroth/Azeroth.wdt";
    LPCSTR slash     = strrchr(path, '/');
    LPCSTR backslash = strrchr(path, '\\');
    LPCSTR base;
    size_t dir_len, name_len;

    memset(cm_wow_height_cache, 0, sizeof(cm_wow_height_cache));
    cm_wow_height_cache_stamp = 0;
    cm_wow_map_dir[0]  = '\0';
    cm_wow_map_name[0] = '\0';

    base = slash && backslash ? MAX(slash, backslash)
         : slash              ? slash
         : backslash;
    base = base ? base + 1 : path;

    dir_len = (size_t)(base - path);
    if (dir_len > 0) {
        dir_len = MIN(dir_len, sizeof(cm_wow_map_dir) - 1);
        memcpy(cm_wow_map_dir, path, dir_len);
        cm_wow_map_dir[dir_len] = '\0';
        if (dir_len > 0 && (cm_wow_map_dir[dir_len-1] == '/' || cm_wow_map_dir[dir_len-1] == '\\'))
            cm_wow_map_dir[dir_len-1] = '\0';
    }

    name_len = strlen(base);
    if (name_len > 4 && !strcasecmp(base + name_len - 4, ".wdt"))
        name_len -= 4;
    name_len = MIN(name_len, sizeof(cm_wow_map_name) - 1);
    memcpy(cm_wow_map_name, base, name_len);
    cm_wow_map_name[name_len] = '\0';

    if (!cm_wow_map_dir[0] && cm_wow_map_name[0])
        snprintf(cm_wow_map_dir, sizeof(cm_wow_map_dir), "World/Maps/%s", cm_wow_map_name);
}

static int CM_WowAdtIndexForWorldCoord(float coord) {
    return (int)floorf(32.0f - coord / CM_WOW_ADT_SIZE);
}

static LPCSTR CM_WowAdtPath(int tile_x, int tile_y, LPSTR out, DWORD out_size) {
    if (!cm_wow_map_dir[0] || !cm_wow_map_name[0] || !out || !out_size)
        return NULL;
    snprintf(out, out_size, "%s/%s_%d_%d.adt", cm_wow_map_dir, cm_wow_map_name, tile_x, tile_y);
    return out;
}

static void CM_WowLoadAdtHeights(cmWowAdtHeightCache_t *cache, int tile_x, int tile_y) {
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, offset = 0;

    memset(cache, 0, sizeof(*cache));
    cache->loaded = true;
    cache->tile_x = tile_x;
    cache->tile_y = tile_y;
    cache->stamp = ++cm_wow_height_cache_stamp;

    if (!CM_WowAdtPath(tile_x, tile_y, path, sizeof(path)))
        return;

    data = FS_ReadFile(path, &size);
    if (!data || !size) {
        SAFE_DELETE(data, FS_FreeFile);
        return;
    }

    while (offset + 8 <= size) {
        BYTE const *tag        = data + offset;
        DWORD       chunk_size = CM_WowRead32(data + offset + 4);
        BYTE const *chunk      = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size)
            break;

        if (CM_WowTagEquals(tag, "KNCM") && chunk_size >= 0x80) {
            DWORD sub     = 0x80;
            DWORD index_x = CM_WowRead32(chunk + 0x04);
            DWORD index_y = CM_WowRead32(chunk + 0x08);
            cmWowChunkHeight_t *height_chunk = NULL;

            if (index_x < 16 && index_y < 16) {
                height_chunk = &cache->chunks[index_y][index_x];
                height_chunk->area_id = CM_WowRead32(chunk + 0x34);
                memcpy(&height_chunk->position, chunk + 0x68, sizeof(height_chunk->position));
            }

            while (height_chunk && sub + 8 <= chunk_size) {
                BYTE const *subtag  = chunk + sub;
                DWORD       sub_size = CM_WowRead32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                BOOL        is_mcnr  = CM_WowTagEquals(subtag, "RNCM");

                sub += 8;
                if (sub + sub_size > chunk_size)
                    break;
                if (CM_WowTagEquals(subtag, "TVCM") && sub_size >= sizeof(height_chunk->heights)) {
                    memcpy(height_chunk->heights, subchunk, sizeof(height_chunk->heights));
                    height_chunk->has_heights    = true;
                    cache->valid                 = true;
                }
                sub += sub_size;
                if (is_mcnr && sub_size == 145 * 3 && sub + 13 <= chunk_size)
                    sub += 13;
            }
        }
        offset += chunk_size;
    }
    FS_FreeFile(data);
}

/* A fixed LRU keeps adjacent creature tiles hot without heap work or an unbounded world cache. */
static cmWowAdtHeightCache_t *CM_WowHeightCache(int tile_x, int tile_y) {
    cmWowAdtHeightCache_t *oldest = &cm_wow_height_cache[0];

    FOR_LOOP(i, CM_WOW_ADT_CACHE_SIZE) {
        cmWowAdtHeightCache_t *cache = &cm_wow_height_cache[i];

        if (cache->loaded && cache->tile_x == tile_x && cache->tile_y == tile_y) {
            cache->stamp = ++cm_wow_height_cache_stamp;
            return cache;
        }
        if (!cache->loaded || cache->stamp < oldest->stamp) oldest = cache;
    }
    CM_WowLoadAdtHeights(oldest, tile_x, tile_y);
    return oldest;
}

/* MCNK indices map directly from the tile edge; the old 256-chunk scan ran for every sample. */
static cmWowChunkHeight_t const *CM_WowChunkAtPoint(cmWowAdtHeightCache_t const *cache, FLOAT sx, FLOAT sy) {
    int row = (int)floorf(((32.0f - cache->tile_y) * CM_WOW_ADT_SIZE - sx) / CM_WOW_ADT_CHUNK_SIZE);
    int col = (int)floorf(((32.0f - cache->tile_x) * CM_WOW_ADT_SIZE - sy) / CM_WOW_ADT_CHUNK_SIZE);
    cmWowChunkHeight_t const *chunk;

    if (row < 0 || row >= 16 || col < 0 || col >= 16) return NULL;
    chunk = &cache->chunks[row][col];
    if (!chunk->has_heights ||
        sx > chunk->position.x + 0.001f ||
        sx < chunk->position.x - 8.0f * CM_WOW_ADT_UNIT_SIZE - 0.001f ||
        sy > chunk->position.y + 0.001f ||
        sy < chunk->position.y - 8.0f * CM_WOW_ADT_UNIT_SIZE - 0.001f)
        return NULL;
    return chunk;
}

static BOOL CM_WowBarycentricHeight(float px, float py,
                                    float ax, float ay, float ah,
                                    float bx, float by, float bh,
                                    float cx, float cy, float ch,
                                    float *height) {
    float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    float wa, wb, wc;

    if (fabsf(den) < 0.000001f || !height)
        return false;
    wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
    wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
    wc = 1.0f - wa - wb;
    if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f)
        return false;
    *height = wa * ah + wb * bh + wc * ch;
    return true;
}

static BOOL CM_WowHeightInCell(float const *heights, int row, int col, float fx, float fy, float *height) {
    int   base = row * 17 + col;
    float h_tl = heights[base],     h_tr = heights[base + 1];
    float h_bl = heights[base + 17], h_br = heights[base + 18];
    float h_c  = heights[base + 9];

    return CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  0.0f, 0.0f, h_tl, 1.0f, 0.0f, h_bl, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  0.0f, 1.0f, h_tr, 0.0f, 0.0f, h_tl, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  1.0f, 1.0f, h_br, 0.0f, 1.0f, h_tr, height) ||
           CM_WowBarycentricHeight(fx, fy, 0.5f, 0.5f, h_c,  1.0f, 0.0f, h_bl, 1.0f, 1.0f, h_br, height);
}

static BOOL CM_WowTerrainHeightAtPoint(FLOAT sx, FLOAT sy, FLOAT *height) {
    int tile_x = CM_WowAdtIndexForWorldCoord(sy);
    int tile_y = CM_WowAdtIndexForWorldCoord(sx);
    cmWowAdtHeightCache_t *cache;
    cmWowChunkHeight_t const *chunk;
    float local_row, local_col, cell_height;
    int cell_row, cell_col;

    if (!height || tile_x < 0 || tile_x >= 64 || tile_y < 0 || tile_y >= 64)
        return false;
    cache = CM_WowHeightCache(tile_x, tile_y);
    if (!cache->valid || !(chunk = CM_WowChunkAtPoint(cache, sx, sy))) return false;
    local_row = (chunk->position.x - sx) / CM_WOW_ADT_UNIT_SIZE;
    local_col = (chunk->position.y - sy) / CM_WOW_ADT_UNIT_SIZE;
    cell_row = (int)floorf(MIN(local_row, 7.9999f));
    cell_col = (int)floorf(MIN(local_col, 7.9999f));
    if (cell_row < 0 || cell_row >= 8 || cell_col < 0 || cell_col >= 8) return false;
    if (!CM_WowHeightInCell(chunk->heights, cell_row, cell_col,
                            local_row - cell_row, local_col - cell_col, &cell_height))
        return false;
    *height = chunk->position.z + cell_height;
    return true;
}

static BOOL CM_WowValidDbc(BYTE const *data, DWORD size,
                            DWORD *records, DWORD *fields,
                            DWORD *record_size, DWORD *string_size) {
    if (!data || size <= 20 || memcmp(data, "WDBC", 4) != 0)
        return false;
    *records     = CM_WowRead32(data + 4);
    *fields      = CM_WowRead32(data + 8);
    *record_size = CM_WowRead32(data + 12);
    *string_size = CM_WowRead32(data + 16);
    if (*fields == 0 || *record_size < *fields * sizeof(DWORD) ||
        20 + *records * *record_size + *string_size > size)
        return false;
    return true;
}

static DWORD CM_WowAreaIdAtPoint(FLOAT sx, FLOAT sy) {
    int tile_x = CM_WowAdtIndexForWorldCoord(sy);
    int tile_y = CM_WowAdtIndexForWorldCoord(sx);
    cmWowAdtHeightCache_t *cache;
    cmWowChunkHeight_t const *chunk;

    if (tile_x < 0 || tile_x >= 64 || tile_y < 0 || tile_y >= 64) return 0;
    cache = CM_WowHeightCache(tile_x, tile_y);
    chunk = CM_WowChunkAtPoint(cache, sx, sy);
    return chunk ? chunk->area_id : 0;
}

/* Resolve the MCNK area ID through the English name field in classic AreaTable.dbc. */
LPCSTR CM_WowAreaNameAtPoint(FLOAT sx, FLOAT sy) {
    static DWORD cached_id = ~0u;
    static char cached_name[128];
    DWORD area_id = CM_WowAreaIdAtPoint(sx, sy);
    LPBYTE data;
    DWORD size = 0, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;

    if (!area_id) return NULL;
    if (area_id == cached_id) return cached_name[0] ? cached_name : NULL;
    cached_id = area_id;
    cached_name[0] = '\0';
    data = FS_ReadFile("DBFilesClient\\AreaTable.dbc", &size);
    if (!CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size) || fields <= 11) {
        fprintf(stderr, "CM_WowAreaNameAtPoint: invalid DBFilesClient\\AreaTable.dbc\n");
        SAFE_DELETE(data, FS_FreeFile);
        return NULL;
    }
    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(i, records) {
        BYTE const *record = records_base + i * record_size;
        LPCSTR name;

        if (CM_WowRead32(record) != area_id) continue;
        name = CM_WowDbcString(strings_base, string_size, CM_WowRead32(record + 11 * sizeof(DWORD)));
        if (name) snprintf(cached_name, sizeof(cached_name), "%s", name);
        FS_FreeFile(data);
        return cached_name[0] ? cached_name : NULL;
    }
    fprintf(stderr, "CM_WowAreaNameAtPoint: AreaTable.dbc has no area id %u\n", (unsigned)area_id);
    FS_FreeFile(data);
    return NULL;
}

static BOOL CM_WowFindMapId(LPCSTR map_name, DWORD *map_id) {
    LPBYTE data;
    DWORD size = 0, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;

    if (!map_name || !*map_name || !map_id)
        return false;

    data = FS_ReadFile("DBFilesClient\\Map.dbc", &size);
    if (!CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size)) {
        SAFE_DELETE(data, FS_FreeFile);
        return false;
    }
    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        FOR_LOOP(field_index, fields) {
            DWORD string_offset = CM_WowRead32(record + field_index * sizeof(DWORD));
            LPCSTR value = CM_WowDbcString(strings_base, string_size, string_offset);
            if (value && *value && !strcasecmp(value, map_name)) {
                *map_id = CM_WowRead32(record);
                FS_FreeFile(data);
                return true;
            }
        }
    }
    FS_FreeFile(data);
    return false;
}

static LPCSTR CM_WowWorldSafeLocName(BYTE const *record, DWORD fields,
                                      BYTE const *strings_base, DWORD string_size) {
    for (DWORD field_index = 5; field_index < fields; field_index++) {
        DWORD string_offset = CM_WowRead32(record + field_index * sizeof(DWORD));
        LPCSTR value = CM_WowDbcString(strings_base, string_size, string_offset);
        if (value && *value)
            return value;
    }
    return NULL;
}

static DWORD CM_WowCollectWorldSafeLocs(DWORD map_id, LPVECTOR3 first_spawn,
                                        LPSTR first_name, size_t first_name_size) {
    LPBYTE data;
    DWORD size = 0, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;
    DWORD count = 0;

    if (!first_spawn)
        return 0;

    data = FS_ReadFile("DBFilesClient\\WorldSafeLocs.dbc", &size);
    if (!CM_WowValidDbc(data, size, &records, &fields, &record_size, &string_size) ||
        fields < 5 || record_size < 5 * sizeof(DWORD)) {
        SAFE_DELETE(data, FS_FreeFile);
        return 0;
    }
    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        cmWowWorldSafeLoc_t const *safe_loc = (cmWowWorldSafeLoc_t const *)record;
        LPCSTR name;
        mapPlayer_t *player;

        if (safe_loc->map_id != map_id)
            continue;

        name = CM_WowWorldSafeLocName(record, fields, strings_base, string_size);
        if (count == 0) {
            memcpy(first_spawn, &safe_loc->position, sizeof(*first_spawn));
            if (first_name && first_name_size) {
                first_name[0] = '\0';
                if (name) {
                    strncpy(first_name, name, first_name_size - 1);
                    first_name[first_name_size - 1] = '\0';
                }
            }
        }
        if (count < MAX_PLAYERS) {
            player = &world.info.players[count];
            player->used = true;
            player->playerType = count == 0 ? kPlayerTypeHuman : kPlayerTypeNone;
            player->playerName = CM_WowCopyString(name);
            player->startingPosition = (VECTOR2){ safe_loc->position.x, safe_loc->position.y };
            cm_wow_spawn_heights[count] = safe_loc->position.z;
        }
        count++;
    }
    FS_FreeFile(data);
    return count;
}

static void CM_WowChooseSpawn(LPCSTR mapFilename) {
    char map_name[128]      = { 0 };
    char safe_loc_name[128] = { 0 };
    DWORD map_id = 0;
    DWORD safe_loc_count = 0;
    BOOL has_map_id, has_safe_locs;

    cm_wow_spawn_position = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    memset(cm_wow_spawn_heights, 0, sizeof(cm_wow_spawn_heights));
    world.info.players[0].used       = true;
    world.info.players[0].playerType = kPlayerTypeHuman;
    CM_WowSetMapPath(mapFilename);

    if (!CM_WowExtractMapName(mapFilename, map_name, sizeof(map_name))) {
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };
        fprintf(stderr, "CM_LoadMap: WoW spawn fallback at %.3f %.3f %.3f (no map name)\n",
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
        return;
    }

    has_map_id   = CM_WowFindMapId(map_name, &map_id);
    if (has_map_id)
        safe_loc_count = CM_WowCollectWorldSafeLocs(map_id, &cm_wow_spawn_position,
                                                    safe_loc_name, sizeof(safe_loc_name));
    has_safe_locs = safe_loc_count > 0;
    if (!has_safe_locs)
        world.info.players[0].startingPosition = (VECTOR2){ cm_wow_spawn_position.x, cm_wow_spawn_position.y };

    if (has_safe_locs)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u loaded %u WorldSafeLocs spawn candidates, first%s%s at %.3f %.3f %.3f\n",
                map_name, (unsigned)map_id,
                (unsigned)safe_loc_count,
                safe_loc_name[0] ? " " : "", safe_loc_name,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else if (has_map_id)
        fprintf(stderr, "CM_LoadMap: WoW map %s id=%u has no WorldSafeLocs entry, spawn fallback at %.3f %.3f %.3f\n",
                map_name, (unsigned)map_id,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
    else
        fprintf(stderr, "CM_LoadMap: WoW map %s has no Map.dbc entry, spawn fallback at %.3f %.3f %.3f\n",
                map_name,
                cm_wow_spawn_position.x, cm_wow_spawn_position.y, cm_wow_spawn_position.z);
}

/* ---- public API ---- */

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    memset(&world, 0, sizeof(world));
    if (mapFilename) {
        size_t len = strlen(mapFilename);
        world.info.mapName = MemAlloc(len + 1);
        memcpy(world.info.mapName, mapFilename, len + 1);
    }
    CM_WowChooseSpawn(mapFilename);
    return true;
}

/* This is the single gameplay floor contract; later surfaces compete here without changing callers. */
BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result) {
    FLOAT center, left, right, down, up;
    BOOL has_left, has_right, has_down, has_up;

    if (result) *result = (WOWGROUNDRESULT){ .normal = { 0.0f, 0.0f, 1.0f } };
    if (!query || !result || !isfinite(query->origin.x) || !isfinite(query->origin.y) ||
        !isfinite(query->origin.z) || !isfinite(query->max_down) || !isfinite(query->max_up) ||
        query->max_down < 0.0f || query->max_up < 0.0f ||
        !CM_WowTerrainHeightAtPoint(query->origin.x, query->origin.y, &center) ||
        center < query->origin.z - query->max_down ||
        center > query->origin.z + query->max_up)
        return false;

    has_left = CM_WowTerrainHeightAtPoint(query->origin.x - CM_WOW_ADT_UNIT_SIZE,
                                          query->origin.y, &left);
    has_right = CM_WowTerrainHeightAtPoint(query->origin.x + CM_WOW_ADT_UNIT_SIZE,
                                           query->origin.y, &right);
    has_down = CM_WowTerrainHeightAtPoint(query->origin.x,
                                          query->origin.y - CM_WOW_ADT_UNIT_SIZE, &down);
    has_up = CM_WowTerrainHeightAtPoint(query->origin.x,
                                        query->origin.y + CM_WOW_ADT_UNIT_SIZE, &up);
    result->normal = (VECTOR3){
        has_left && has_right ? left - right
            : has_left ? 2.0f * (left - center) : has_right ? 2.0f * (center - right) : 0.0f,
        has_down && has_up ? down - up
            : has_down ? 2.0f * (down - center) : has_up ? 2.0f * (center - up) : 0.0f,
        2.0f * CM_WOW_ADT_UNIT_SIZE,
    };
    Vector3_normalize(&result->normal);
    result->height = center;
    result->surface = WOW_SURFACE_TERRAIN;
    return true;
}

FLOAT CM_GetHeightAtPoint(FLOAT sx, FLOAT sy) {
    FLOAT terrain_height;
    if (CM_WowTerrainHeightAtPoint(sx, sy, &terrain_height))
        return terrain_height;
    FOR_LOOP(i, MAX_PLAYERS) {
        if (world.info.players[i].used &&
            fabsf(world.info.players[i].startingPosition.x - sx) < 0.001f &&
            fabsf(world.info.players[i].startingPosition.y - sy) < 0.001f)
            return cm_wow_spawn_heights[i];
    }
    return cm_wow_spawn_position.z;
}

VECTOR2 CM_GetNormalizedMapPosition(FLOAT x, FLOAT y) {
    return (VECTOR2){ x, y };
}

VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) {
    return (VECTOR2){ x, y };
}

BOX2 CM_GetWorldBounds(void) {
    return (BOX2){
        .min = { -32768.0f, -32768.0f },
        .max = {  32768.0f,  32768.0f },
    };
}
