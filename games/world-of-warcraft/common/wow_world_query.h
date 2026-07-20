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
    FLOAT max_walkable_up;
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

typedef struct {
    VECTOR3 start;
    VECTOR3 displacement;
    FLOAT radius;
    FLOAT height;
} WOWSWEEPQUERY;
typedef WOWSWEEPQUERY *LPWOWSWEEPQUERY;
typedef WOWSWEEPQUERY const *LPCWOWSWEEPQUERY;

typedef struct {
    VECTOR3 end;
    VECTOR3 normal;
    FLOAT fraction;
    FLOAT penetration;
    wowSurfaceType_t surface;
    BOOL start_solid;
} WOWSWEEPRESULT;
typedef WOWSWEEPRESULT *LPWOWSWEEPRESULT;
typedef WOWSWEEPRESULT const *LPCWOWSWEEPRESULT;

BOOL CM_WowQueryGround(LPCWOWGROUNDQUERY query, LPWOWGROUNDRESULT result);
BOOL CM_WowSweepWorld(LPCWOWSWEEPQUERY query, LPWOWSWEEPRESULT result);

#endif
