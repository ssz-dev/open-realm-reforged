#include "r_wowmap.h"

/* WoW previously dropped circle splats; project their full diameter onto the existing terrain-conforming mesh. */
void R_RenderSplat(LPCVECTOR2 position, float radius, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    VECTOR2 mins;
    VECTOR2 maxs;

    mins = (VECTOR2){ position->x - radius, position->y - radius };
    maxs = (VECTOR2){ position->x + radius, position->y + radius };
    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}
