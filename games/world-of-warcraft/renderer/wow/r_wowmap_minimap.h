#ifndef R_WOWMAP_MINIMAP_H
#define R_WOWMAP_MINIMAP_H

/* Resolve the client-era logical tile name through md5translate.trs. */
static BOOL Wow_FindMinimapTilePath(LPCSTR translations, LPCSTR map_name,
                                    int tile_x, int tile_y, LPSTR out, DWORD out_size) {
    char wanted[256];
    LPCSTR line;

    if (!translations || !map_name || !*map_name || !out || !out_size) return false;
    snprintf(wanted, sizeof(wanted), "%s\\map%02d_%02d.blp", map_name, tile_x, tile_y);
    for (line = translations; *line; ) {
        char logical[256], hash[128];
        LPCSTR next = strchr(line, '\n');

        memset(logical, 0, sizeof(logical));
        memset(hash, 0, sizeof(hash));
        if (sscanf(line, "%255[^\t]\t%127[^\r\n]", logical, hash) == 2 && !strcasecmp(logical, wanted)) {
            snprintf(out, out_size, "textures\\Minimap\\%s", hash);
            return true;
        }
        line = next ? next + 1 : line + strlen(line);
    }
    return false;
}

/* WoW ADT indices run opposite native coordinates; return the within-tile texture center. */
static VECTOR2 Wow_MinimapTileCenter(FLOAT world_x, FLOAT world_y, int tile_x, int tile_y) {
    return MAKE(VECTOR2,
                32.0f - world_x / WOW_ADT_SIZE - tile_y,
                32.0f - world_y / WOW_ADT_SIZE - tile_x);
}

#endif
