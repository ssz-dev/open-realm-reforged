#ifndef WOW_WMO_FORMAT_H
#define WOW_WMO_FORMAT_H

#include "common/shared.h"
#include <stdio.h>
#include <string.h>

#define WOW_WMO_GROUP_HEADER_SIZE 0x44
#define WOW_WMO_ADT_SIZE 533.333313f
#define WOW_WMO_WORLD_OFFSET (32.0f * WOW_WMO_ADT_SIZE)

typedef struct { FLOAT x, y, z; } WOWVEC3;
typedef WOWVEC3 *LPWOWVEC3;
typedef WOWVEC3 const *LPCWOWVEC3;

typedef struct { FLOAT u, v; } WOWVEC2;
typedef WOWVEC2 *LPWOWVEC2;
typedef WOWVEC2 const *LPCWOWVEC2;

typedef struct { WOWVEC3 min, max; } WOWBOX;
typedef WOWBOX *LPWOWBOX;
typedef WOWBOX const *LPCWOWBOX;

typedef struct {
    DWORD name_id, unique_id;
    WOWVEC3 position, rotation;
    WORD scale, flags;
} WOWDOODADDEF;
typedef WOWDOODADDEF *LPWOWDOODADDEF;
typedef WOWDOODADDEF const *LPCWOWDOODADDEF;

typedef struct {
    DWORD name_id, unique_id;
    WOWVEC3 position, rotation;
    WOWBOX extents;
    WORD flags, doodad_set, name_set, scale;
} WOWMAPOBJDEF;
typedef WOWMAPOBJDEF *LPWOWMAPOBJDEF;
typedef WOWMAPOBJDEF const *LPCWOWMAPOBJDEF;

typedef struct { BYTE flags, material_id; } WOWWMOPOLY;
typedef WOWWMOPOLY *LPWOWWMOPOLY;
typedef WOWWMOPOLY const *LPCWOWWMOPOLY;

typedef struct {
    SHORT box_min[3], box_max[3];
    DWORD first_index;
    WORD num_indices, first_vertex, last_vertex;
    BYTE flags, material_id;
} WOWWMOBATCHDEF;
typedef WOWWMOBATCHDEF *LPWOWWMOBATCHDEF;
typedef WOWWMOBATCHDEF const *LPCWOWWMOBATCHDEF;

typedef struct {
    SHORT plane_type;
    SHORT children[2];
    WORD num_faces;
    DWORD first_face;
    FLOAT distance;
} WOWWMOBSPNODE;
typedef WOWWMOBSPNODE *LPWOWWMOBSPNODE;
typedef WOWWMOBSPNODE const *LPCWOWWMOBSPNODE;

typedef struct {
    DWORD group_count;
    LPCSTR textures;
    DWORD texture_size;
    BYTE const *materials;
    DWORD material_count;
} WOWWMOROOTVIEW;
typedef WOWWMOROOTVIEW *LPWOWWMOROOTVIEW;
typedef WOWWMOROOTVIEW const *LPCWOWWMOROOTVIEW;

typedef struct {
    DWORD flags;
    WOWBOX bounds;
    LPCWOWWMOPOLY polygons;
    DWORD polygon_count;
    WORD const *indices;
    DWORD index_count;
    LPCWOWVEC3 vertices;
    DWORD vertex_count;
    LPCWOWVEC2 uvs;
    DWORD uv_count;
    LPCWOWWMOBATCHDEF batches;
    DWORD batch_count;
    LPCWOWWMOBSPNODE bsp_nodes;
    DWORD bsp_node_count;
    WORD const *bsp_refs;
    DWORD bsp_ref_count;
} WOWWMOGROUPVIEW;
typedef WOWWMOGROUPVIEW *LPWOWWMOGROUPVIEW;
typedef WOWWMOGROUPVIEW const *LPCWOWWMOGROUPVIEW;

typedef struct {
    LPCSTR blob;
    DWORD blob_size;
    DWORD const *offsets;
    DWORD offset_count, index;
} WOWWMOREFERENCE;
typedef WOWWMOREFERENCE *LPWOWWMOREFERENCE;
typedef WOWWMOREFERENCE const *LPCWOWWMOREFERENCE;

typedef struct {
    LPCSTR root;
    DWORD group;
    LPSTR out;
    DWORD out_size;
} WOWWMOGROUPPATH;
typedef WOWWMOGROUPPATH *LPWOWWMOGROUPPATH;
typedef WOWWMOGROUPPATH const *LPCWOWWMOGROUPPATH;

static DWORD WowWmo_Read32(BYTE const *p) {
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static BOOL WowWmo_TagEquals(BYTE const *tag, LPCSTR reversed) { return !memcmp(tag, reversed, 4); }

static LPCSTR WowWmo_StringAt(LPCSTR blob, DWORD size, DWORD offset) {
    if (!blob || offset >= size || !memchr(blob + offset, '\0', size - offset)) return NULL;
    return blob + offset;
}

static LPCSTR WowWmo_StringRef(LPCWOWWMOREFERENCE ref) {
    return ref->offsets && ref->index < ref->offset_count
        ? WowWmo_StringAt(ref->blob, ref->blob_size, ref->offsets[ref->index]) : NULL;
}

static void WowWmo_GroupPath(LPCWOWWMOGROUPPATH path) {
    size_t len = path->root ? strlen(path->root) : 0;

    if (!path->out || !path->out_size) return;
    if (len > 4 && !strcasecmp(path->root + len - 4, ".wmo"))
        snprintf(path->out, path->out_size, "%.*s_%03u.wmo",
                 (int)(len - 4), path->root, (unsigned)path->group);
    else
        snprintf(path->out, path->out_size, "%s_%03u.wmo",
                 path->root ? path->root : "", (unsigned)path->group);
}

/* Root parsing is shared by renderer and collision so chunk interpretation cannot drift. */
static BOOL WowWmo_ParseRoot(BYTE const *data, DWORD size, LPWOWWMOROOTVIEW view) {
    DWORD offset = 0;

    if (!data || !view) return false;
    *view = (WOWWMOROOTVIEW){ 0 };
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = WowWmo_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size) return false;
        if (WowWmo_TagEquals(tag, "DHOM") && chunk_size >= 8)
            view->group_count = WowWmo_Read32(chunk + 4);
        else if (WowWmo_TagEquals(tag, "XTOM"))
            view->textures = (LPCSTR)chunk, view->texture_size = chunk_size;
        else if (WowWmo_TagEquals(tag, "TMOM"))
            view->materials = chunk, view->material_count = chunk_size / 64;
        offset += chunk_size;
    }
    return view->group_count > 0;
}

/* Group parsing retains MOBN/MOBR beside draw data; MOBR is the collision face contract. */
static BOOL WowWmo_ParseGroup(BYTE const *data, DWORD size, LPWOWWMOGROUPVIEW view) {
    DWORD offset = 0;
    BOOL found_group = false;

    if (!data || !view) return false;
    *view = (WOWWMOGROUPVIEW){ 0 };
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = WowWmo_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size) return false;
        if (WowWmo_TagEquals(tag, "PGOM")) {
            DWORD sub = WOW_WMO_GROUP_HEADER_SIZE;

            if (chunk_size < sub) return false;
            view->flags = WowWmo_Read32(chunk + 8);
            memcpy(&view->bounds.min, chunk + 12, sizeof(view->bounds.min));
            memcpy(&view->bounds.max, chunk + 24, sizeof(view->bounds.max));
            while (sub + 8 <= chunk_size) {
                BYTE const *subtag = chunk + sub;
                DWORD sub_size = WowWmo_Read32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;

                sub += 8;
                if (sub + sub_size > chunk_size) return false;
                if (WowWmo_TagEquals(subtag, "YPOM"))
                    view->polygons = (LPCWOWWMOPOLY)subchunk,
                    view->polygon_count = sub_size / sizeof(WOWWMOPOLY);
                else if (WowWmo_TagEquals(subtag, "IVOM"))
                    view->indices = (WORD const *)subchunk, view->index_count = sub_size / sizeof(WORD);
                else if (WowWmo_TagEquals(subtag, "TVOM"))
                    view->vertices = (LPCWOWVEC3)subchunk, view->vertex_count = sub_size / sizeof(WOWVEC3);
                else if (WowWmo_TagEquals(subtag, "VTOM"))
                    view->uvs = (LPCWOWVEC2)subchunk, view->uv_count = sub_size / sizeof(WOWVEC2);
                else if (WowWmo_TagEquals(subtag, "ABOM"))
                    view->batches = (LPCWOWWMOBATCHDEF)subchunk,
                    view->batch_count = sub_size / sizeof(WOWWMOBATCHDEF);
                else if (WowWmo_TagEquals(subtag, "NBOM"))
                    view->bsp_nodes = (LPCWOWWMOBSPNODE)subchunk,
                    view->bsp_node_count = sub_size / sizeof(WOWWMOBSPNODE);
                else if (WowWmo_TagEquals(subtag, "RBOM"))
                    view->bsp_refs = (WORD const *)subchunk, view->bsp_ref_count = sub_size / sizeof(WORD);
                sub += sub_size;
            }
            found_group = true;
        }
        offset += chunk_size;
    }
    return found_group;
}

static VECTOR3 WowWmo_ObjectPoint(LPCWOWVEC3 p) {
    return (VECTOR3){ WOW_WMO_WORLD_OFFSET - p->z, WOW_WMO_WORLD_OFFSET - p->x, p->y };
}

/* The shared transform exactly matches ADT MODF rendering, including classic fixed-point scale. */
static void WowWmo_InstanceMatrix(LPCWOWMAPOBJDEF def, LPMATRIX4 matrix) {
    MATRIX4 basis, tmp;
    VECTOR3 origin = WowWmo_ObjectPoint(&def->position);
    FLOAT scale = def->scale ? def->scale / 1024.0f : 1.0f;

    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &origin);
    Matrix4_identity(&basis);
    basis.v[0] = 0.0f; basis.v[1] = 1.0f; basis.v[2] = 0.0f;
    basis.v[4] = 0.0f; basis.v[5] = 0.0f; basis.v[6] = 1.0f;
    basis.v[8] = 1.0f; basis.v[9] = 0.0f; basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &basis, &tmp);
    *matrix = tmp;
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, def->rotation.y - 270.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -def->rotation.x }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ def->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){ scale, scale, scale });
}

#endif
