#ifndef WOW_COLLISION_LOCAL_H
#define WOW_COLLISION_LOCAL_H

#include "common/wow_world_query.h"
#include "common/wow_wmo_format.h"

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

typedef struct {
    LPCMWOWCOLLISIONINSTANCE instances;
    DWORD instance_count;
} CMWOWCOLLISIONTILE;
typedef CMWOWCOLLISIONTILE *LPCMWOWCOLLISIONTILE;
typedef CMWOWCOLLISIONTILE const *LPCCMWOWCOLLISIONTILE;

void CM_WowCollisionReset(void);
void CM_WowCollisionTileFree(LPCMWOWCOLLISIONTILE tile);
BOOL CM_WowCollisionTileLoad(LPCMWOWCOLLISIONTILE tile, BYTE const *data, DWORD size);
BOOL CM_WowCollisionGroundTile(LPCCMWOWCOLLISIONTILE tile,
                               LPCWOWGROUNDQUERY query,
                               LPWOWGROUNDRESULT result);
BOOL CM_WowCollisionSweepTile(LPCCMWOWCOLLISIONTILE tile,
                              LPCWOWSWEEPQUERY query,
                              LPWOWSWEEPRESULT result);

#endif
