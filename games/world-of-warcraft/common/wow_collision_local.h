#ifndef WOW_COLLISION_LOCAL_H
#define WOW_COLLISION_LOCAL_H

#include "common/wow_world_query.h"
#include "common/wow_m2_format.h"
#include "common/wow_wmo_format.h"

typedef struct { VECTOR3 a, b, c; } CMWOWTRIANGLE;
typedef CMWOWTRIANGLE *LPCMWOWTRIANGLE;
typedef CMWOWTRIANGLE const *LPCCMWOWTRIANGLE;

typedef struct {
    LPCWOWSWEEPQUERY query;
    CMWOWTRIANGLE triangle;
    wowSurfaceType_t surface;
} CMWOWTRIANGLESWEEP;
typedef CMWOWTRIANGLESWEEP const *LPCCMWOWTRIANGLESWEEP;

typedef struct {
    WOWBOX bounds;
    LPWOWVEC3 vertices;
    DWORD vertex_count;
    LPDWORD faces;
    DWORD face_count;
    DWORD index_count;
    DWORD bsp_node_count;
    DWORD bsp_ref_count;
} CMWOWCOLLISIONGROUP;
typedef CMWOWCOLLISIONGROUP *LPCMWOWCOLLISIONGROUP;
typedef CMWOWCOLLISIONGROUP const *LPCCMWOWCOLLISIONGROUP;

typedef struct CMWOWCOLLISIONMODEL_s {
    PATHSTR path;
    LPCMWOWCOLLISIONGROUP groups;
    DWORD group_count;
    BOOL valid;
    struct CMWOWCOLLISIONMODEL_s *next;
} CMWOWCOLLISIONMODEL;
typedef CMWOWCOLLISIONMODEL *LPCMWOWCOLLISIONMODEL;
typedef CMWOWCOLLISIONMODEL const *LPCCMWOWCOLLISIONMODEL;

typedef struct {
    LPCMWOWCOLLISIONMODEL model;
    MATRIX4 matrix;
    LPWOWBOX group_bounds;
    WOWBOX bounds;
} CMWOWCOLLISIONINSTANCE;
typedef CMWOWCOLLISIONINSTANCE *LPCMWOWCOLLISIONINSTANCE;
typedef CMWOWCOLLISIONINSTANCE const *LPCCMWOWCOLLISIONINSTANCE;

typedef struct CMWOWDOODADMODEL_s {
    PATHSTR path;
    LPVECTOR3 vertices;
    WORD *indices;
    DWORD vertex_count;
    DWORD index_count;
    DWORD references;
    WOWBOX bounds;
    wowM2CollisionStatus_t status;
    struct CMWOWDOODADMODEL_s *next;
} CMWOWDOODADMODEL;
typedef CMWOWDOODADMODEL *LPCMWOWDOODADMODEL;
typedef CMWOWDOODADMODEL const *LPCCMWOWDOODADMODEL;

typedef struct {
    LPCMWOWDOODADMODEL model;
    MATRIX4 matrix;
    WOWBOX bounds;
} CMWOWDOODADINSTANCE;
typedef CMWOWDOODADINSTANCE *LPCMWOWDOODADINSTANCE;
typedef CMWOWDOODADINSTANCE const *LPCCMWOWDOODADINSTANCE;

typedef struct {
    LPCMWOWCOLLISIONINSTANCE instances;
    DWORD instance_count;
    LPCMWOWDOODADMODEL *doodad_models;
    DWORD doodad_model_count;
    LPCMWOWDOODADINSTANCE doodad_instances;
    DWORD doodad_instance_count;
    DWORD doodad_chunk_offsets[257];
    LPDWORD doodad_chunk_refs;
    VECTOR2 doodad_tile_max;
    VECTOR2 doodad_max_extent;
    BOOL doodad_grid_valid;
} CMWOWCOLLISIONTILE;
typedef CMWOWCOLLISIONTILE *LPCMWOWCOLLISIONTILE;
typedef CMWOWCOLLISIONTILE const *LPCCMWOWCOLLISIONTILE;

/* Broadphase bounds cover the whole vertical capsule path for every surface provider. */
static WOWBOX CM_WowCollisionSweepBounds(LPCWOWSWEEPQUERY query) {
    VECTOR3 end = Vector3_add(&query->start, &query->displacement);
    return (WOWBOX){
        .min = {
            MIN(query->start.x, end.x) - query->radius,
            MIN(query->start.y, end.y) - query->radius,
            MIN(query->start.z, end.z),
        },
        .max = {
            MAX(query->start.x, end.x) + query->radius,
            MAX(query->start.y, end.y) + query->radius,
            MAX(query->start.z, end.z) + query->height,
        },
    };
}

void CM_WowCollisionReset(void);
void CM_WowCollisionTileFree(LPCMWOWCOLLISIONTILE tile);
BOOL CM_WowCollisionTileLoad(LPCMWOWCOLLISIONTILE tile, BYTE const *data, DWORD size);
BOOL CM_WowCollisionSweepTriangle(LPCCMWOWTRIANGLESWEEP sweep, LPWOWSWEEPRESULT result);
BOOL CM_WowCollisionGroundTile(LPCCMWOWCOLLISIONTILE tile,
                               LPCWOWGROUNDQUERY query,
                               LPWOWGROUNDRESULT result);
BOOL CM_WowCollisionSweepTile(LPCCMWOWCOLLISIONTILE tile,
                              LPCWOWSWEEPQUERY query,
                              LPWOWSWEEPRESULT result);

#endif
