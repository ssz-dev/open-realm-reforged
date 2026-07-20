#ifndef WOW_WORLD_QUERY_H
#define WOW_WORLD_QUERY_H

#include "common/shared.h"

typedef enum {
    WOW_SURFACE_NONE,
    WOW_SURFACE_TERRAIN,
    WOW_SURFACE_WMO,
    WOW_SURFACE_DOODAD,
} wowSurfaceType_t;

typedef struct {
    VECTOR3 origin;
    FLOAT max_down;
    FLOAT max_up;
} WOWGROUNDQUERY;
typedef WOWGROUNDQUERY *LPWOWGROUNDQUERY;
typedef WOWGROUNDQUERY const *LPCWOWGROUNDQUERY;

typedef struct {
    FLOAT height;
    VECTOR3 normal;
    wowSurfaceType_t surface;
} WOWGROUNDRESULT;
typedef WOWGROUNDRESULT *LPWOWGROUNDRESULT;
typedef WOWGROUNDRESULT const *LPCWOWGROUNDRESULT;

BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result);

#endif
