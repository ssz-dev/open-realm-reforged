#include "common/common.h"
#include "common/wow_collision_local.h"
#include "common/wow_world_query.h"
#include <float.h>
#include <math.h>

#define CM_WOW_WMO_POLY_DETAIL    0x04
#define CM_WOW_WMO_POLY_COLLISION 0x08
#define CM_WOW_WMO_POLY_RENDER    0x20
#define CM_WOW_COLLISION_EPSILON  0.0001f
#define CM_WOW_WALKABLE_NORMAL_Z  0.642788f

typedef struct { VECTOR3 a, b, c; } CMWOWTRIANGLE;
typedef CMWOWTRIANGLE *LPCMWOWTRIANGLE;
typedef CMWOWTRIANGLE const *LPCCMWOWTRIANGLE;

typedef struct {
    VECTOR3 start, displacement;
    FLOAT radius;
    CMWOWTRIANGLE triangle;
} CMWOWSPHERESWEEP;
typedef CMWOWSPHERESWEEP const *LPCCMWOWSPHERESWEEP;

typedef struct {
    FLOAT fraction, penetration;
    VECTOR3 normal;
    BOOL start_solid;
} CMWOWSPHERERESULT;
typedef CMWOWSPHERERESULT *LPCMWOWSPHERERESULT;

typedef struct { VECTOR3 start, end; } CMWOWEDGE;
typedef CMWOWEDGE const *LPCCMWOWEDGE;

typedef struct {
    LPCCMWOWCOLLISIONGROUP group;
    DWORD face;
    LPCMATRIX4 matrix;
} CMWOWGROUPFACE;
typedef CMWOWGROUPFACE const *LPCCMWOWGROUPFACE;

static LPCMWOWCOLLISIONMODEL cm_wow_collision_models;

static WOWBOX CM_WowEmptyBox(void) {
    return (WOWBOX){ .min = { FLT_MAX, FLT_MAX, FLT_MAX }, .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX } };
}

static void CM_WowAddBoxPoint(LPWOWBOX box, LPCVECTOR3 p) {
    box->min.x = MIN(box->min.x, p->x); box->min.y = MIN(box->min.y, p->y); box->min.z = MIN(box->min.z, p->z);
    box->max.x = MAX(box->max.x, p->x); box->max.y = MAX(box->max.y, p->y); box->max.z = MAX(box->max.z, p->z);
}

static BOOL CM_WowBoxValid(LPCWOWBOX box) {
    return box && box->min.x <= box->max.x && box->min.y <= box->max.y && box->min.z <= box->max.z;
}

static BOOL CM_WowBoxesIntersect(LPCWOWBOX a, LPCWOWBOX b) {
    return a->min.x <= b->max.x && a->max.x >= b->min.x &&
           a->min.y <= b->max.y && a->max.y >= b->min.y &&
           a->min.z <= b->max.z && a->max.z >= b->min.z;
}

static WOWBOX CM_WowTransformBox(LPCWOWBOX source, LPCMATRIX4 matrix) {
    WOWBOX out = CM_WowEmptyBox();

    FOR_LOOP(i, 8) {
        VECTOR3 p = {
            (i & 1) ? source->max.x : source->min.x,
            (i & 2) ? source->max.y : source->min.y,
            (i & 4) ? source->max.z : source->min.z,
        };
        p = Matrix4_multiply_vector3(matrix, &p);
        CM_WowAddBoxPoint(&out, &p);
    }
    return out;
}

static VECTOR3 CM_WowTriangleNormal(LPCCMWOWTRIANGLE triangle) {
    VECTOR3 ab = Vector3_sub(&triangle->b, &triangle->a);
    VECTOR3 ac = Vector3_sub(&triangle->c, &triangle->a);
    VECTOR3 normal = Vector3_cross(&ab, &ac);

    if (Vector3_lengthsq(&normal) <= CM_WOW_COLLISION_EPSILON * CM_WOW_COLLISION_EPSILON)
        return (VECTOR3){ 0.0f, 0.0f, 0.0f };
    Vector3_normalize(&normal);
    return normal;
}

static BOOL CM_WowPointInTriangle(LPCVECTOR3 point, LPCCMWOWTRIANGLE triangle, LPCVECTOR3 normal) {
    VECTOR3 ab = Vector3_sub(&triangle->b, &triangle->a);
    VECTOR3 bc = Vector3_sub(&triangle->c, &triangle->b);
    VECTOR3 ca = Vector3_sub(&triangle->a, &triangle->c);
    VECTOR3 ap = Vector3_sub(point, &triangle->a);
    VECTOR3 bp = Vector3_sub(point, &triangle->b);
    VECTOR3 cp = Vector3_sub(point, &triangle->c);
    VECTOR3 c0 = Vector3_cross(&ab, &ap);
    VECTOR3 c1 = Vector3_cross(&bc, &bp);
    VECTOR3 c2 = Vector3_cross(&ca, &cp);

    return Vector3_dot(&c0, normal) >= -CM_WOW_COLLISION_EPSILON &&
           Vector3_dot(&c1, normal) >= -CM_WOW_COLLISION_EPSILON &&
           Vector3_dot(&c2, normal) >= -CM_WOW_COLLISION_EPSILON;
}

/* RTCD's Voronoi-region test gives the penetration normal for starts inside geometry. */
static VECTOR3 CM_WowClosestTrianglePoint(LPCVECTOR3 point, LPCCMWOWTRIANGLE triangle) {
    VECTOR3 ab = Vector3_sub(&triangle->b, &triangle->a);
    VECTOR3 ac = Vector3_sub(&triangle->c, &triangle->a);
    VECTOR3 ap = Vector3_sub(point, &triangle->a);
    FLOAT d1 = Vector3_dot(&ab, &ap), d2 = Vector3_dot(&ac, &ap);
    VECTOR3 bp, cp, bc;
    FLOAT d3, d4, d5, d6, vc, vb, va, denom;
    VECTOR3 edge_point, face_point;

    if (d1 <= 0.0f && d2 <= 0.0f) return triangle->a;
    bp = Vector3_sub(point, &triangle->b);
    d3 = Vector3_dot(&ab, &bp); d4 = Vector3_dot(&ac, &bp);
    if (d3 >= 0.0f && d4 <= d3) return triangle->b;
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return Vector3_mad(&triangle->a, d1 / (d1 - d3), &ab);
    cp = Vector3_sub(point, &triangle->c);
    d5 = Vector3_dot(&ab, &cp); d6 = Vector3_dot(&ac, &cp);
    if (d6 >= 0.0f && d5 <= d6) return triangle->c;
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return Vector3_mad(&triangle->a, d2 / (d2 - d6), &ac);
    va = d3 * d6 - d5 * d4;
    bc = Vector3_sub(&triangle->c, &triangle->b);
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
        return Vector3_mad(&triangle->b, (d4 - d3) / ((d4 - d3) + (d5 - d6)), &bc);
    denom = 1.0f / (va + vb + vc);
    edge_point = Vector3_mad(&triangle->a, vb * denom, &ab);
    face_point = Vector3_scale(&ac, vc * denom);
    return Vector3_add(&edge_point, &face_point);
}

static void CM_WowSphereCandidate(LPCMWOWSPHERERESULT result, FLOAT fraction, LPCVECTOR3 normal) {
    if (fraction < -CM_WOW_COLLISION_EPSILON || fraction > result->fraction) return;
    result->fraction = MAX(0.0f, fraction);
    result->normal = *normal;
}

static void CM_WowSweepSphereVertex(LPCCMWOWSPHERESWEEP sweep, LPCVECTOR3 vertex,
                                    LPCMWOWSPHERERESULT result) {
    VECTOR3 relative = Vector3_sub(&sweep->start, vertex);
    FLOAT a = Vector3_dot(&sweep->displacement, &sweep->displacement);
    FLOAT b = 2.0f * Vector3_dot(&relative, &sweep->displacement);
    FLOAT c = Vector3_dot(&relative, &relative) - sweep->radius * sweep->radius;
    FLOAT discriminant, fraction;
    VECTOR3 center, normal;

    if (a <= CM_WOW_COLLISION_EPSILON || (discriminant = b * b - 4.0f * a * c) < 0.0f) return;
    fraction = (-b - sqrtf(discriminant)) / (2.0f * a);
    if (fraction < 0.0f || fraction > result->fraction) return;
    center = Vector3_mad(&sweep->start, fraction, &sweep->displacement);
    normal = Vector3_sub(&center, vertex);
    if (Vector3_lengthsq(&normal) <= CM_WOW_COLLISION_EPSILON) return;
    Vector3_normalize(&normal);
    if (Vector3_dot(&sweep->displacement, &normal) < 0.0f)
        CM_WowSphereCandidate(result, fraction, &normal);
}

static void CM_WowSweepSphereEdge(LPCCMWOWSPHERESWEEP sweep, LPCCMWOWEDGE segment,
                                  LPCMWOWSPHERERESULT result) {
    VECTOR3 edge = Vector3_sub(&segment->end, &segment->start);
    VECTOR3 relative = Vector3_sub(&sweep->start, &segment->start);
    FLOAT edge_len_sq = Vector3_dot(&edge, &edge);
    FLOAT start_axis, velocity_axis;
    VECTOR3 start_perp, velocity_perp;
    FLOAT a, b, c, discriminant, fraction, along;
    VECTOR3 center, contact, normal;
    VECTOR3 axis_component;

    if (edge_len_sq <= CM_WOW_COLLISION_EPSILON) return;
    start_axis = Vector3_dot(&relative, &edge) / edge_len_sq;
    velocity_axis = Vector3_dot(&sweep->displacement, &edge) / edge_len_sq;
    axis_component = Vector3_scale(&edge, start_axis);
    start_perp = Vector3_sub(&relative, &axis_component);
    axis_component = Vector3_scale(&edge, velocity_axis);
    velocity_perp = Vector3_sub(&sweep->displacement, &axis_component);
    a = Vector3_dot(&velocity_perp, &velocity_perp);
    b = 2.0f * Vector3_dot(&start_perp, &velocity_perp);
    c = Vector3_dot(&start_perp, &start_perp) - sweep->radius * sweep->radius;
    if (a <= CM_WOW_COLLISION_EPSILON || (discriminant = b * b - 4.0f * a * c) < 0.0f) return;
    fraction = (-b - sqrtf(discriminant)) / (2.0f * a);
    along = start_axis + velocity_axis * fraction;
    if (fraction < 0.0f || fraction > result->fraction || along <= 0.0f || along >= 1.0f) return;
    center = Vector3_mad(&sweep->start, fraction, &sweep->displacement);
    contact = Vector3_mad(&segment->start, along, &edge);
    normal = Vector3_sub(&center, &contact);
    if (Vector3_lengthsq(&normal) <= CM_WOW_COLLISION_EPSILON) return;
    Vector3_normalize(&normal);
    if (Vector3_dot(&sweep->displacement, &normal) < 0.0f)
        CM_WowSphereCandidate(result, fraction, &normal);
}

/* Face, edge-cylinder, and vertex roots prevent tunnelling through thin WMO triangles. */
static BOOL CM_WowSweepSphereTriangle(LPCCMWOWSPHERESWEEP sweep, LPCMWOWSPHERERESULT result) {
    VECTOR3 normal = CM_WowTriangleNormal(&sweep->triangle);
    VECTOR3 closest = CM_WowClosestTrianglePoint(&sweep->start, &sweep->triangle);
    VECTOR3 separation = Vector3_sub(&sweep->start, &closest);
    VECTOR3 plane_relative = Vector3_sub(&sweep->start, &sweep->triangle.a);
    FLOAT distance_sq = Vector3_lengthsq(&separation);
    FLOAT plane_start, plane_velocity;

    *result = (CMWOWSPHERERESULT){ .fraction = 1.0f };
    if (Vector3_lengthsq(&normal) <= CM_WOW_COLLISION_EPSILON) return false;
    if (distance_sq < sweep->radius * sweep->radius - CM_WOW_COLLISION_EPSILON) {
        FLOAT distance = sqrtf(MAX(0.0f, distance_sq));

        result->start_solid = true;
        result->penetration = sweep->radius - distance;
        result->normal = distance > CM_WOW_COLLISION_EPSILON ? Vector3_scale(&separation, 1.0f / distance) : normal;
        if (Vector3_dot(&result->normal, &sweep->displacement) > 0.0f)
            result->normal = Vector3_scale(&result->normal, -1.0f);
        result->fraction = 0.0f;
        return true;
    }

    plane_start = Vector3_dot(&normal, &plane_relative);
    plane_velocity = Vector3_dot(&normal, &sweep->displacement);
    FOR_LOOP(side_index, 2) {
        FLOAT side = side_index ? -1.0f : 1.0f;
        FLOAT signed_start = plane_start * side;
        FLOAT signed_velocity = plane_velocity * side;
        FLOAT fraction;
        VECTOR3 center, contact, face_normal = Vector3_scale(&normal, side);

        if (signed_start < sweep->radius - CM_WOW_COLLISION_EPSILON ||
            signed_velocity >= -CM_WOW_COLLISION_EPSILON)
            continue;
        fraction = (sweep->radius - signed_start) / signed_velocity;
        if (fraction < 0.0f || fraction > result->fraction) continue;
        center = Vector3_mad(&sweep->start, fraction, &sweep->displacement);
        contact = Vector3_mad(&center, -sweep->radius, &face_normal);
        if (CM_WowPointInTriangle(&contact, &sweep->triangle, &normal))
            CM_WowSphereCandidate(result, fraction, &face_normal);
    }
    CM_WowSweepSphereEdge(sweep, &(CMWOWEDGE){ sweep->triangle.a, sweep->triangle.b }, result);
    CM_WowSweepSphereEdge(sweep, &(CMWOWEDGE){ sweep->triangle.b, sweep->triangle.c }, result);
    CM_WowSweepSphereEdge(sweep, &(CMWOWEDGE){ sweep->triangle.c, sweep->triangle.a }, result);
    CM_WowSweepSphereVertex(sweep, &sweep->triangle.a, result);
    CM_WowSweepSphereVertex(sweep, &sweep->triangle.b, result);
    CM_WowSweepSphereVertex(sweep, &sweep->triangle.c, result);
    return result->fraction < 1.0f;
}

static BOOL CM_WowGroupTriangle(LPCCMWOWGROUPFACE source, LPCMWOWTRIANGLE triangle) {
    LPCCMWOWCOLLISIONGROUP group = source->group;
    DWORD polygon = group->faces[source->face];
    DWORD first_index = polygon * 3;
    DWORD ia, ib, ic;
    VECTOR3 a, b, c;

    if (first_index + 2 >= group->index_count) return false;
    ia = group->faces[group->face_count + first_index];
    ib = group->faces[group->face_count + first_index + 1];
    ic = group->faces[group->face_count + first_index + 2];
    if (ia >= group->vertex_count || ib >= group->vertex_count || ic >= group->vertex_count) return false;
    memcpy(&a, &group->vertices[ia], sizeof(a));
    memcpy(&b, &group->vertices[ib], sizeof(b));
    memcpy(&c, &group->vertices[ic], sizeof(c));
    *triangle = (CMWOWTRIANGLE){
        Matrix4_multiply_vector3(source->matrix, &a),
        Matrix4_multiply_vector3(source->matrix, &b),
        Matrix4_multiply_vector3(source->matrix, &c),
    };
    return true;
}

static void CM_WowCollisionGroupFree(LPCMWOWCOLLISIONGROUP group) {
    SAFE_DELETE(group->vertices, MemFree);
    SAFE_DELETE(group->faces, MemFree);
    memset(group, 0, sizeof(*group));
}

/* MOBN leaf ranges select unique MOBR faces; explicit MOPY fallback is logged when classic data lacks BSP chunks. */
static BOOL CM_WowCollisionGroupLoad(LPCMWOWCOLLISIONMODEL model, DWORD group_index,
                                     LPCMWOWCOLLISIONGROUP group) {
    PATHSTR path;
    LPBYTE data;
    DWORD size = 0, polygon_count, face_count = 0, invalid_refs = 0;
    WOWWMOGROUPVIEW view;
    LPBYTE selected;

    WowWmo_GroupPath(&(WOWWMOGROUPPATH){
        .root = model->path, .group = group_index, .out = path, .out_size = sizeof(path),
    });
    data = FS_ReadFile(path, &size);
    if (!data || !WowWmo_ParseGroup(data, size, &view) || !view.vertices || !view.indices) {
        fprintf(stderr, "OpenWoW collision: malformed WMO group %s\n", path);
        SAFE_DELETE(data, FS_FreeFile);
        return false;
    }
    polygon_count = MIN(view.polygon_count, view.index_count / 3);
    if (!polygon_count) {
        fprintf(stderr, "OpenWoW collision: WMO group %s has no polygons\n", path);
        FS_FreeFile(data);
        return false;
    }
    selected = MemAlloc(polygon_count);
    memset(selected, 0, polygon_count);
    if (view.bsp_nodes && view.bsp_refs) {
        DWORD leaves = 0;

        FOR_LOOP(i, view.bsp_node_count) {
            LPCWOWWMOBSPNODE node = view.bsp_nodes + i;

            if (!(node->plane_type & 4)) continue;
            leaves++;
            if (node->first_face + node->num_faces > view.bsp_ref_count) {
                fprintf(stderr, "OpenWoW collision: invalid MOBN leaf range in %s\n", path);
                MemFree(selected);
                FS_FreeFile(data);
                return false;
            }
            FOR_LOOP(j, node->num_faces) {
                DWORD polygon = view.bsp_refs[node->first_face + j];
                if (polygon < polygon_count) selected[polygon] = 1;
                else invalid_refs++;
            }
        }
        if (!leaves) {
            fprintf(stderr, "OpenWoW collision: MOBN in %s has no leaves\n", path);
            MemFree(selected);
            FS_FreeFile(data);
            return false;
        }
    } else {
        fprintf(stderr, "OpenWoW collision: %s lacks MOBN/MOBR; using explicit MOPY collision flags\n", path);
        FOR_LOOP(i, polygon_count) {
            LPCWOWWMOPOLY polygon = view.polygons + i;
            selected[i] = polygon->material_id == 0xff || (polygon->flags & CM_WOW_WMO_POLY_COLLISION) ||
                          ((polygon->flags & CM_WOW_WMO_POLY_RENDER) &&
                           !(polygon->flags & CM_WOW_WMO_POLY_DETAIL));
        }
    }
    if (invalid_refs)
        fprintf(stderr, "OpenWoW collision: %s ignored %u out-of-range MOBR references\n",
                path, (unsigned)invalid_refs);
    FOR_LOOP(i, polygon_count)
        if (selected[i] && view.indices[i * 3] < view.vertex_count &&
            view.indices[i * 3 + 1] < view.vertex_count && view.indices[i * 3 + 2] < view.vertex_count)
            face_count++;

    group->bounds = view.bounds;
    group->vertex_count = view.vertex_count;
    group->vertices = MemAlloc(sizeof(*group->vertices) * view.vertex_count);
    memcpy(group->vertices, view.vertices, sizeof(*group->vertices) * view.vertex_count);
    group->face_count = face_count;
    group->bsp_node_count = view.bsp_node_count;
    group->bsp_ref_count = view.bsp_ref_count;
    group->index_count = polygon_count * 3;
    if (face_count) {
        DWORD write = 0;

        group->faces = MemAlloc(sizeof(*group->faces) * (face_count + group->index_count));
        FOR_LOOP(i, polygon_count)
            if (selected[i] && view.indices[i * 3] < view.vertex_count &&
                view.indices[i * 3 + 1] < view.vertex_count && view.indices[i * 3 + 2] < view.vertex_count)
                group->faces[write++] = i;
        FOR_LOOP(i, group->index_count)
            group->faces[group->face_count + i] = view.indices[i];
    }
    MemFree(selected);
    FS_FreeFile(data);
    return true;
}

static void CM_WowCollisionModelFree(LPCMWOWCOLLISIONMODEL model) {
    FOR_LOOP(i, model->group_count) CM_WowCollisionGroupFree(&model->groups[i]);
    SAFE_DELETE(model->groups, MemFree);
    MemFree(model);
}

static LPCMWOWCOLLISIONMODEL CM_WowCollisionModel(LPCSTR path) {
    LPCMWOWCOLLISIONMODEL model;
    LPBYTE data;
    DWORD size = 0;
    WOWWMOROOTVIEW view;

    for (model = cm_wow_collision_models; model; model = model->next)
        if (!strcasecmp(model->path, path)) return model->valid ? model : NULL;
    model = MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->next = cm_wow_collision_models;
    cm_wow_collision_models = model;
    data = FS_ReadFile(path, &size);
    if (!data || !WowWmo_ParseRoot(data, size, &view)) {
        fprintf(stderr, "OpenWoW collision: malformed WMO root %s\n", path);
        SAFE_DELETE(data, FS_FreeFile);
        return NULL;
    }
    model->groups = MemAlloc(sizeof(*model->groups) * view.group_count);
    memset(model->groups, 0, sizeof(*model->groups) * view.group_count);
    model->group_count = view.group_count;
    FOR_LOOP(i, view.group_count)
        if (!CM_WowCollisionGroupLoad(model, i, &model->groups[i])) {
            FS_FreeFile(data);
            return NULL;
        }
    FS_FreeFile(data);
    model->valid = true;
    return model;
}

void CM_WowCollisionReset(void) {
    while (cm_wow_collision_models) {
        LPCMWOWCOLLISIONMODEL next = cm_wow_collision_models->next;
        CM_WowCollisionModelFree(cm_wow_collision_models);
        cm_wow_collision_models = next;
    }
}

void CM_WowCollisionTileFree(LPCMWOWCOLLISIONTILE tile) {
    if (!tile) return;
    FOR_LOOP(i, tile->instance_count) SAFE_DELETE(tile->instances[i].group_bounds, MemFree);
    SAFE_DELETE(tile->instances, MemFree);
    memset(tile, 0, sizeof(*tile));
}

/* ADT MWMO/MWID/MODF is loaded with the terrain tile, avoiding a second archive read or full-world scan. */
BOOL CM_WowCollisionTileLoad(LPCMWOWCOLLISIONTILE tile, BYTE const *data, DWORD size) {
    DWORD offset = 0, definition_count = 0;
    LPCSTR names = NULL;
    DWORD names_size = 0;
    DWORD const *name_offsets = NULL;
    DWORD name_offset_count = 0;
    LPCWOWMAPOBJDEF definitions = NULL;

    if (!tile || !data) return false;
    CM_WowCollisionTileFree(tile);
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = WowWmo_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;

        offset += 8;
        if (offset + chunk_size > size) {
            fprintf(stderr, "OpenWoW collision: truncated ADT object chunk\n");
            return false;
        }
        if (WowWmo_TagEquals(tag, "OMWM"))
            names = (LPCSTR)chunk, names_size = chunk_size;
        else if (WowWmo_TagEquals(tag, "DIWM"))
            name_offsets = (DWORD const *)chunk, name_offset_count = chunk_size / sizeof(DWORD);
        else if (WowWmo_TagEquals(tag, "FDOM"))
            definitions = (LPCWOWMAPOBJDEF)chunk, definition_count = chunk_size / sizeof(WOWMAPOBJDEF);
        offset += chunk_size;
    }
    if (!definition_count) return true;
    if (!names || !name_offsets) {
        fprintf(stderr, "OpenWoW collision: ADT MODF lacks MWMO/MWID names\n");
        return false;
    }
    tile->instances = MemAlloc(sizeof(*tile->instances) * definition_count);
    memset(tile->instances, 0, sizeof(*tile->instances) * definition_count);
    FOR_LOOP(i, definition_count) {
        LPCWOWMAPOBJDEF definition = definitions + i;
        WOWWMOREFERENCE reference = {
            .blob = names, .blob_size = names_size, .offsets = name_offsets,
            .offset_count = name_offset_count, .index = definition->name_id,
        };
        LPCSTR path = WowWmo_StringRef(&reference);
        LPCMWOWCOLLISIONMODEL model;
        LPCMWOWCOLLISIONINSTANCE instance;

        if (!path) {
            fprintf(stderr, "OpenWoW collision: ADT MODF %u has invalid WMO name %u\n",
                    (unsigned)i, (unsigned)definition->name_id);
            continue;
        }
        model = CM_WowCollisionModel(path);
        if (!model) continue;
        instance = &tile->instances[tile->instance_count++];
        instance->model = model;
        instance->bounds = CM_WowEmptyBox();
        WowWmo_InstanceMatrix(definition, &instance->matrix);
        instance->group_bounds = MemAlloc(sizeof(*instance->group_bounds) * model->group_count);
        FOR_LOOP(group_index, model->group_count) {
            instance->group_bounds[group_index] =
                CM_WowTransformBox(&model->groups[group_index].bounds, &instance->matrix);
            if (!CM_WowBoxValid(&instance->group_bounds[group_index])) continue;
            CM_WowAddBoxPoint(&instance->bounds, (LPCVECTOR3)&instance->group_bounds[group_index].min);
            CM_WowAddBoxPoint(&instance->bounds, (LPCVECTOR3)&instance->group_bounds[group_index].max);
        }
    }
    return true;
}

static BOOL CM_WowTriangleHeight(LPCCMWOWTRIANGLE triangle, LPCVECTOR2 point, FLOAT *height) {
    FLOAT den = (triangle->b.y - triangle->c.y) * (triangle->a.x - triangle->c.x) +
                (triangle->c.x - triangle->b.x) * (triangle->a.y - triangle->c.y);
    FLOAT wa, wb, wc;

    if (fabsf(den) <= CM_WOW_COLLISION_EPSILON) return false;
    wa = ((triangle->b.y - triangle->c.y) * (point->x - triangle->c.x) +
          (triangle->c.x - triangle->b.x) * (point->y - triangle->c.y)) / den;
    wb = ((triangle->c.y - triangle->a.y) * (point->x - triangle->c.x) +
          (triangle->a.x - triangle->c.x) * (point->y - triangle->c.y)) / den;
    wc = 1.0f - wa - wb;
    if (wa < -CM_WOW_COLLISION_EPSILON || wb < -CM_WOW_COLLISION_EPSILON ||
        wc < -CM_WOW_COLLISION_EPSILON)
        return false;
    *height = wa * triangle->a.z + wb * triangle->b.z + wc * triangle->c.z;
    return true;
}

BOOL CM_WowCollisionGroundTile(LPCCMWOWCOLLISIONTILE tile,
                               LPCWOWGROUNDQUERY query,
                               LPWOWGROUNDRESULT result) {
    BOOL found = false;
    FLOAT best = -FLT_MAX;
    FLOAT highest = query->origin.z + MIN(query->max_up, query->max_walkable_up);
    WOWBOX query_box = {
        .min = { query->origin.x, query->origin.y, query->origin.z - query->max_down },
        .max = { query->origin.x, query->origin.y, highest },
    };
    VECTOR2 point = { query->origin.x, query->origin.y };

    if (!tile) return false;
    FOR_LOOP(instance_index, tile->instance_count) {
        LPCCMWOWCOLLISIONINSTANCE instance = &tile->instances[instance_index];

        CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_INSTANCE_TESTS, 1);
        if (!CM_WowBoxesIntersect(&instance->bounds, &query_box)) continue;
        CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_BROADPHASE_CANDIDATES, 1);
        FOR_LOOP(group_index, instance->model->group_count) {
            LPCCMWOWCOLLISIONGROUP group = &instance->model->groups[group_index];

            CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_GROUP_TESTS, 1);
            if (!CM_WowBoxesIntersect(&instance->group_bounds[group_index], &query_box)) continue;
            FOR_LOOP(face, group->face_count) {
                CMWOWTRIANGLE triangle;
                VECTOR3 normal;
                FLOAT height;

                CMWOWGROUPFACE source = { group, face, &instance->matrix };

                CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_TRIANGLE_TESTS, 1);
                if (!CM_WowGroupTriangle(&source, &triangle)) continue;
                normal = CM_WowTriangleNormal(&triangle);
                if (fabsf(normal.z) < CM_WOW_WALKABLE_NORMAL_Z ||
                    !CM_WowTriangleHeight(&triangle, &point, &height) ||
                    height < query_box.min.z || height > query_box.max.z || height <= best)
                    continue;
                if (normal.z < 0.0f) normal = Vector3_scale(&normal, -1.0f);
                best = height;
                result->height = height;
                result->normal = normal;
                result->surface = WOW_SURFACE_WMO;
                found = true;
            }
        }
    }
    return found;
}

static WOWBOX CM_WowSweepBounds(LPCWOWSWEEPQUERY query) {
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

BOOL CM_WowCollisionSweepTile(LPCCMWOWCOLLISIONTILE tile,
                              LPCWOWSWEEPQUERY query,
                              LPWOWSWEEPRESULT result) {
    WOWBOX sweep_bounds = CM_WowSweepBounds(query);
    BOOL hit = false;

    if (!tile) return false;
    FOR_LOOP(instance_index, tile->instance_count) {
        LPCCMWOWCOLLISIONINSTANCE instance = &tile->instances[instance_index];

        CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_INSTANCE_TESTS, 1);
        if (!CM_WowBoxesIntersect(&instance->bounds, &sweep_bounds)) continue;
        CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_BROADPHASE_CANDIDATES, 1);
        FOR_LOOP(group_index, instance->model->group_count) {
            LPCCMWOWCOLLISIONGROUP group = &instance->model->groups[group_index];

            CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_WMO_GROUP_TESTS, 1);
            if (!CM_WowBoxesIntersect(&instance->group_bounds[group_index], &sweep_bounds)) continue;
            FOR_LOOP(face, group->face_count) {
                CMWOWTRIANGLE triangle;
                VECTOR3 triangle_normal;
                FLOAT low = MIN(query->radius, query->height * 0.5f);
                FLOAT high = MAX(low, query->height - low);
                DWORD sphere_count = MAX(1u, (DWORD)ceilf((high - low) / MAX(query->radius, 0.1f)) + 1u);

                CMWOWGROUPFACE source = { group, face, &instance->matrix };

                CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_TRIANGLE_TESTS, 1);
                if (!CM_WowGroupTriangle(&source, &triangle)) continue;
                triangle_normal = CM_WowTriangleNormal(&triangle);
                if (fabsf(triangle_normal.z) >= CM_WOW_WALKABLE_NORMAL_Z) continue;
                FOR_LOOP(sphere, sphere_count) {
                    FLOAT offset = sphere_count == 1 ? low :
                        low + (high - low) * (FLOAT)sphere / (FLOAT)(sphere_count - 1);
                    CMWOWSPHERESWEEP sphere_sweep = {
                        .start = { query->start.x, query->start.y, query->start.z + offset },
                        .displacement = query->displacement,
                        .radius = query->radius,
                        .triangle = triangle,
                    };
                    CMWOWSPHERERESULT sphere_result;

                    if (!CM_WowSweepSphereTriangle(&sphere_sweep, &sphere_result)) continue;
                    if (sphere_result.start_solid &&
                        (!result->start_solid || sphere_result.penetration > result->penetration)) {
                        result->start_solid = true;
                        result->fraction = 0.0f;
                        result->penetration = sphere_result.penetration;
                        result->normal = sphere_result.normal;
                        result->surface = WOW_SURFACE_WMO;
                        hit = true;
                    } else if (!result->start_solid && sphere_result.fraction < result->fraction) {
                        result->fraction = sphere_result.fraction;
                        result->normal = sphere_result.normal;
                        result->surface = WOW_SURFACE_WMO;
                        hit = true;
                    }
                }
            }
        }
    }
    return hit;
}
