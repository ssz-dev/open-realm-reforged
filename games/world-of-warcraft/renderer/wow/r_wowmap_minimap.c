#include "r_wowmap.h"
#include "r_wowmap_minimap.h"

static void Wow_FreeMinimapTiles(void) {
    FOR_LOOP(row, 3)
        FOR_LOOP(col, 3)
            SAFE_DELETE(tr.minimap_tiles.tiles[row][col], R_ReleaseTexture);
    memset(&tr.minimap_tiles, 0, sizeof(tr.minimap_tiles));
}

/* Keep the displayed Classic minimap centered continuously across a 3x3 ADT tile window. */
void Wow_UpdateMinimap(int tile_x, int tile_y) {
    static char loaded_map[128];
    static int loaded_x = -1, loaded_y = -1;
    VECTOR2 center = Wow_MinimapTileCenter(tr.viewDef.camerastate[0].origin.x,
                                           tr.viewDef.camerastate[0].origin.y,
                                           tile_x, tile_y);
    LPSTR translations = NULL;
    int size;

    tr.minimap_tiles.center = center;
    tr.minimap_tiles.span = 0.65f;
    if (tr.minimap_tiles.active && tile_x == loaded_x && tile_y == loaded_y &&
        !strcasecmp(loaded_map, wow_world.map_name)) return;
    loaded_x = tile_x;
    loaded_y = tile_y;
    snprintf(loaded_map, sizeof(loaded_map), "%s", wow_world.map_name);
    size = ri.FS_ReadFile("Textures\\Minimap\\md5translate.trs", (void **)&translations);
    if (size <= 0 || !translations) {
        fprintf(stderr, "OpenWoW minimap: missing Textures\\Minimap\\md5translate.trs\n");
        return;
    }
    Wow_FreeMinimapTiles();
    tr.minimap_tiles.center = center;
    tr.minimap_tiles.span = 0.65f;
    FOR_LOOP(row, 3) {
        FOR_LOOP(col, 3) {
            int x = tile_x + (int)row - 1, y = tile_y + (int)col - 1;
            PATHSTR path;

            if (!Wow_FindMinimapTilePath(translations, wow_world.map_name, x, y, path, sizeof(path))) continue;
            tr.minimap_tiles.tiles[row][col] = R_LoadTexture(path);
        }
    }
    ri.FS_FreeFile(translations);
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    tr.minimap_tiles.active = tr.minimap_tiles.tiles[1][1] != NULL;
    if (tr.minimap_tiles.active)
        fprintf(stderr, "OpenWoW minimap: loaded moving 3x3 window for tile %02d,%02d\n", tile_x, tile_y);
    else
        fprintf(stderr, "OpenWoW minimap: missing center tile %02d,%02d for %s\n",
                tile_x, tile_y, wow_world.map_name);
}

void Wow_ShutdownMinimap(void) { Wow_FreeMinimapTiles(); }
