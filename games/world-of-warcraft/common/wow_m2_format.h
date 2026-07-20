#ifndef WOW_M2_FORMAT_H
#define WOW_M2_FORMAT_H

#include "common/wow_wmo_format.h"
#include <math.h>

typedef struct {
    int32_t size;
    int32_t offset;
} WOWM2ARRAY;
typedef WOWM2ARRAY *LPWOWM2ARRAY;
typedef WOWM2ARRAY const *LPCWOWM2ARRAY;

typedef struct { VECTOR3 min, max; } WOWM2BOX;
typedef WOWM2BOX *LPWOWM2BOX;
typedef WOWM2BOX const *LPCWOWM2BOX;

typedef struct {
    DWORD magic;
    DWORD version;
    WOWM2ARRAY name;
    DWORD flags;
    WOWM2ARRAY global_loops;
    WOWM2ARRAY sequences;
    WOWM2ARRAY sequence_lookups;
    WOWM2ARRAY bones;
    WOWM2ARRAY key_bone_lookup;
    WOWM2ARRAY vertices;
    DWORD num_skin_profiles;
    WOWM2ARRAY colors;
    WOWM2ARRAY textures;
    WOWM2ARRAY texture_weights;
    WOWM2ARRAY texture_transforms;
    WOWM2ARRAY replaceable_texture_lookup;
    WOWM2ARRAY materials;
    WOWM2ARRAY bone_lookup_table;
    WOWM2ARRAY texture_lookup_table;
    WOWM2ARRAY tex_unit_lookup_table;
    WOWM2ARRAY transparency_lookup_table;
    WOWM2ARRAY texture_transforms_lookup_table;
    WOWM2BOX bounding_box;
    FLOAT bounding_sphere_radius;
    WOWM2BOX collision_box;
    FLOAT collision_sphere_radius;
    WOWM2ARRAY collision_indices;
    WOWM2ARRAY collision_positions;
    WOWM2ARRAY collision_normals;
    WOWM2ARRAY attachments;
    WOWM2ARRAY attachment_lookup;
    WOWM2ARRAY events;
    WOWM2ARRAY lights;
    WOWM2ARRAY cameras;
    WOWM2ARRAY camera_lookup;
    WOWM2ARRAY ribbons;
    WOWM2ARRAY particles;
    WOWM2ARRAY texture_combiner_combos;
} WOWM2HEADER;
typedef WOWM2HEADER *LPWOWM2HEADER;
typedef WOWM2HEADER const *LPCWOWM2HEADER;

typedef struct {
    DWORD magic;
    DWORD version;
    WOWM2ARRAY name;
    DWORD flags;
    WOWM2ARRAY global_loops;
    WOWM2ARRAY sequences;
    WOWM2ARRAY sequence_lookups;
    WOWM2ARRAY playable_animation_lookup;
    WOWM2ARRAY bones;
    WOWM2ARRAY key_bone_lookup;
    WOWM2ARRAY vertices;
    WOWM2ARRAY views;
    WOWM2ARRAY colors;
    WOWM2ARRAY textures;
    WOWM2ARRAY transparency_lookup;
    WOWM2ARRAY texture_flipbooks;
    WOWM2ARRAY texture_animations;
    WOWM2ARRAY color_replacements;
    WOWM2ARRAY render_flags;
    WOWM2ARRAY bone_lookup_table;
    WOWM2ARRAY texture_lookup_table;
    WOWM2ARRAY tex_unit_lookup_table;
    WOWM2ARRAY transparency_lookup_table;
    WOWM2ARRAY texture_transforms_lookup_table;
    WOWM2BOX bounding_box;
    FLOAT bounding_sphere_radius;
    WOWM2BOX collision_box;
    FLOAT collision_sphere_radius;
    WOWM2ARRAY collision_indices;
    WOWM2ARRAY collision_positions;
    WOWM2ARRAY collision_normals;
    WOWM2ARRAY attachments;
    WOWM2ARRAY attachment_lookup;
    WOWM2ARRAY events;
    WOWM2ARRAY lights;
    WOWM2ARRAY cameras;
    WOWM2ARRAY camera_lookup;
    WOWM2ARRAY ribbons;
    WOWM2ARRAY particles;
} WOWM2HEADERLEGACY;
typedef WOWM2HEADERLEGACY *LPWOWM2HEADERLEGACY;
typedef WOWM2HEADERLEGACY const *LPCWOWM2HEADERLEGACY;

typedef enum {
    WOW_M2_COLLISION_MALFORMED = -1,
    WOW_M2_COLLISION_NONE,
    WOW_M2_COLLISION_VALID,
} wowM2CollisionStatus_t;

typedef struct {
    BYTE const *payload;
    DWORD payload_size;
    DWORD version;
    WORD const *indices;
    DWORD index_count;
    LPCVECTOR3 positions;
    DWORD position_count;
    LPCVECTOR3 normals;
    DWORD normal_count;
    WOWBOX bounds;
} WOWM2COLLISIONVIEW;
typedef WOWM2COLLISIONVIEW *LPWOWM2COLLISIONVIEW;
typedef WOWM2COLLISIONVIEW const *LPCWOWM2COLLISIONVIEW;

typedef struct {
    VECTOR3 origin;
    VECTOR3 rotation;
    FLOAT scale;
} WOWM2INSTANCE;
typedef WOWM2INSTANCE *LPWOWM2INSTANCE;
typedef WOWM2INSTANCE const *LPCWOWM2INSTANCE;

/* All M2 consumers use one overflow-safe file-array validation contract. */
static BOOL WowM2_ArrayRange(WOWM2ARRAY array, DWORD elem_size, DWORD file_size,
                             LPDWORD offset, LPDWORD bytes) {
    if (array.size <= 0 || array.offset < 0 || !elem_size || !offset || !bytes) return false;
    if ((DWORD)array.size > ~0u / elem_size) return false;
    *offset = (DWORD)array.offset; *bytes = (DWORD)array.size * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static LPCVOID WowM2_ArrayPtr(BYTE const *base, DWORD file_size, WOWM2ARRAY array, DWORD elem_size) {
    DWORD offset, bytes;

    return base && WowM2_ArrayRange(array, elem_size, file_size, &offset, &bytes) ? base + offset : NULL;
}

/* Raw MD20 and MD21/12DM chunk wrappers resolve to the same bounded payload view. */
static BOOL WowM2_FindPayload(BYTE const *data, DWORD size, BYTE const **payload, LPDWORD payload_size) {
    DWORD offset = 0;

    if (!data || !payload || !payload_size || size < sizeof(DWORD)) return false;
    if (WowWmo_Read32(data) == ID_MD20) {
        *payload = data; *payload_size = size;
        return true;
    }
    if (WowWmo_Read32(data) != ID_MD21 && WowWmo_Read32(data) != ID_12DM) return false;
    while (offset + 8 <= size) {
        DWORD tag = WowWmo_Read32(data + offset);
        DWORD chunk_size = WowWmo_Read32(data + offset + 4);
        BYTE const *chunk;

        offset += 8;
        if (chunk_size > size - offset) return false;
        chunk = data + offset;
        if (tag == ID_MD20 ||
            ((tag == ID_MD21 || tag == ID_12DM) &&
             chunk_size >= sizeof(DWORD) && WowWmo_Read32(chunk) == ID_MD20)) {
            *payload = chunk; *payload_size = chunk_size;
            return true;
        }
        offset += chunk_size;
    }
    return false;
}

/* Header variants differ before collision/events; version selects the authoritative on-disk offsets. */
static BOOL WowM2_HeaderArrays(BYTE const *payload, DWORD payload_size,
                               LPWOWM2ARRAY indices, LPWOWM2ARRAY positions,
                               LPWOWM2ARRAY normals, LPWOWM2ARRAY events, LPDWORD version) {
    DWORD file_version;

    if (!payload || payload_size < 8 || WowWmo_Read32(payload) != ID_MD20) return false;
    file_version = WowWmo_Read32(payload + 4);
    if (file_version <= 263) {
        LPCWOWM2HEADERLEGACY header;

        if (payload_size < sizeof(*header)) return false;
        header = (LPCWOWM2HEADERLEGACY)payload;
        if (indices) *indices = header->collision_indices;
        if (positions) *positions = header->collision_positions;
        if (normals) *normals = header->collision_normals;
        if (events) *events = header->events;
    } else {
        LPCWOWM2HEADER header;

        if (payload_size < sizeof(*header)) return false;
        header = (LPCWOWM2HEADER)payload;
        if (indices) *indices = header->collision_indices;
        if (positions) *positions = header->collision_positions;
        if (normals) *normals = header->collision_normals;
        if (events) *events = header->events;
    }
    if (version) *version = file_version;
    return true;
}

static BOOL WowM2_FinitePoint(LPCVECTOR3 point) {
    return point && isfinite(point->x) && isfinite(point->y) && isfinite(point->z);
}

/* Only a complete dedicated collision mesh is solid; absent arrays explicitly mean decoration. */
static wowM2CollisionStatus_t WowM2_ParseCollision(BYTE const *data, DWORD size,
                                                   LPWOWM2COLLISIONVIEW view) {
    BYTE const *payload;
    DWORD payload_size, version, degenerate = 0;
    WOWM2ARRAY index_array, position_array, normal_array;

    if (view) *view = (WOWM2COLLISIONVIEW){ 0 };
    if (!view || !WowM2_FindPayload(data, size, &payload, &payload_size) ||
        !WowM2_HeaderArrays(payload, payload_size, &index_array, &position_array,
                            &normal_array, NULL, &version))
        return WOW_M2_COLLISION_MALFORMED;
    view->payload = payload; view->payload_size = payload_size; view->version = version;
    if (!index_array.size && !position_array.size && !normal_array.size) return WOW_M2_COLLISION_NONE;
    if (index_array.size <= 0 || position_array.size <= 0 || index_array.size % 3 ||
        normal_array.size < 0)
        return WOW_M2_COLLISION_MALFORMED;
    view->indices = WowM2_ArrayPtr(payload, payload_size, index_array, sizeof(*view->indices));
    view->positions = WowM2_ArrayPtr(payload, payload_size, position_array, sizeof(*view->positions));
    view->normals = normal_array.size
        ? WowM2_ArrayPtr(payload, payload_size, normal_array, sizeof(*view->normals)) : NULL;
    if (!view->indices || !view->positions || (normal_array.size && !view->normals))
        return WOW_M2_COLLISION_MALFORMED;
    view->index_count = (DWORD)index_array.size; view->position_count = (DWORD)position_array.size;
    view->normal_count = (DWORD)MAX(0, normal_array.size);
    view->bounds = (WOWBOX){
        .min = { INFINITY, INFINITY, INFINITY },
        .max = { -INFINITY, -INFINITY, -INFINITY },
    };
    FOR_LOOP(i, view->position_count) {
        LPCVECTOR3 p = view->positions + i;

        if (!WowM2_FinitePoint(p)) return WOW_M2_COLLISION_MALFORMED;
        view->bounds.min.x = MIN(view->bounds.min.x, p->x);
        view->bounds.min.y = MIN(view->bounds.min.y, p->y);
        view->bounds.min.z = MIN(view->bounds.min.z, p->z);
        view->bounds.max.x = MAX(view->bounds.max.x, p->x);
        view->bounds.max.y = MAX(view->bounds.max.y, p->y);
        view->bounds.max.z = MAX(view->bounds.max.z, p->z);
    }
    FOR_LOOP(i, view->normal_count)
        if (!WowM2_FinitePoint(view->normals + i)) return WOW_M2_COLLISION_MALFORMED;
    FOR_LOOP(i, view->index_count / 3) {
        WORD a = view->indices[i * 3], b = view->indices[i * 3 + 1], c = view->indices[i * 3 + 2];
        VECTOR3 ab, ac, cross;

        if (a >= view->position_count || b >= view->position_count || c >= view->position_count)
            return WOW_M2_COLLISION_MALFORMED;
        ab = Vector3_sub(view->positions + b, view->positions + a);
        ac = Vector3_sub(view->positions + c, view->positions + a);
        cross = Vector3_cross(&ab, &ac);
        if (Vector3_lengthsq(&cross) <= 0.00000001f) degenerate++;
    }
    return degenerate < view->index_count / 3 ? WOW_M2_COLLISION_VALID : WOW_M2_COLLISION_MALFORMED;
}

/* Classic ADTs name M2 payloads as .mdx while model archives store the same path as .m2. */
static BOOL WowM2_ArchivePath(LPCSTR path, LPSTR out, DWORD out_size) {
    size_t length;

    if (!path || !out || out_size < 4) return false;
    length = strlen(path);
    if (length < 4 || strcasecmp(path + length - 4, ".mdx") || length >= out_size) return false;
    memcpy(out, path, length - 4);
    snprintf(out + length - 4, out_size - (DWORD)length + 4, ".m2");
    return true;
}

/* Non-ground M2 rendering and physics share the same ADT-to-world transform. */
static void WowM2_InstanceMatrix(LPCWOWM2INSTANCE instance, LPMATRIX4 matrix) {
    MATRIX4 basis, tmp;

    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &instance->origin);
    Matrix4_identity(&basis);
    basis.v[0] = 0.0f; basis.v[1] = 1.0f; basis.v[2] = 0.0f;
    basis.v[4] = 0.0f; basis.v[5] = 0.0f; basis.v[6] = 1.0f;
    basis.v[8] = 1.0f; basis.v[9] = 0.0f; basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &basis, &tmp);
    *matrix = tmp;
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, instance->rotation.y - 90.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -instance->rotation.x }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ instance->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){ instance->scale, instance->scale, instance->scale });
}

static void WowM2_DoodadMatrix(LPCWOWDOODADDEF def, LPMATRIX4 matrix) {
    WowM2_InstanceMatrix(&(WOWM2INSTANCE){
        .origin = WowWmo_ObjectPoint(&def->position),
        .rotation = { def->rotation.x, def->rotation.y, def->rotation.z },
        .scale = def->scale / 1024.0f,
    }, matrix);
}

#endif
