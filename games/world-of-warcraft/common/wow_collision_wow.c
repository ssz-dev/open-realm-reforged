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
#define CM_WOW_COLLISION_CHUNK_SIZE (WOW_WMO_ADT_SIZE / 16.0f)

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

typedef struct {
    LPCCMWOWCOLLISIONTILE tile;
    LPCWOWSWEEPQUERY query;
    LPWOWSWEEPRESULT result;
    LPCWOWBOX sweep_bounds;
} CMWOWDOODADSWEEP;
typedef CMWOWDOODADSWEEP const *LPCCMWOWDOODADSWEEP;

static LPCMWOWCOLLISIONMODEL cm_wow_collision_models;
static LPCMWOWDOODADMODEL cm_wow_doodad_models;

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

/* Terrain and WMO callers share the same analytic sphere/triangle narrowphase. */
BOOL CM_WowCollisionSweepTriangle(LPCCMWOWTRIANGLESWEEP sweep, LPWOWSWEEPRESULT result) {
    LPCWOWSWEEPQUERY query = sweep->query;
    FLOAT low = MIN(query->radius, query->height * 0.5f);
    FLOAT high = MAX(low, query->height - low);
    DWORD sphere_count = MAX(1u, (DWORD)ceilf((high - low) / MAX(query->radius, 0.1f)) + 1u);
    BOOL hit = false;

    FOR_LOOP(sphere, sphere_count) {
        FLOAT offset = sphere_count == 1 ? low :
            low + (high - low) * (FLOAT)sphere / (FLOAT)(sphere_count - 1);
        CMWOWSPHERESWEEP sphere_sweep = {
            .start = { query->start.x, query->start.y, query->start.z + offset },
            .displacement = query->displacement,
            .radius = query->radius,
            .triangle = sweep->triangle,
        };
        CMWOWSPHERERESULT sphere_result;

        if (!CM_WowSweepSphereTriangle(&sphere_sweep, &sphere_result)) continue;
        if (sphere_result.start_solid &&
            (!result->start_solid || sphere_result.penetration > result->penetration)) {
            result->start_solid = true;
            result->fraction = 0.0f;
            result->penetration = sphere_result.penetration;
            result->normal = sphere_result.normal;
            result->surface = sweep->surface;
            hit = true;
        } else if (!result->start_solid && sphere_result.fraction < result->fraction) {
            result->fraction = sphere_result.fraction;
            result->normal = sphere_result.normal;
            result->surface = sweep->surface;
            hit = true;
        }
    }
    return hit;
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

static void CM_WowDoodadModelFree(LPCMWOWDOODADMODEL model) {
    SAFE_DELETE(model->vertices, MemFree);
    SAFE_DELETE(model->indices, MemFree);
    MemFree(model);
}

/* One refcounted M2 mesh is retained per active tile set, including negative decoration results. */
static LPCMWOWDOODADMODEL CM_WowDoodadModelAcquire(LPCSTR path) {
    LPCMWOWDOODADMODEL model;
    WOWM2COLLISIONVIEW view;
    LPBYTE data;
    DWORD size = 0;
    PATHSTR archive_path;

    for (model = cm_wow_doodad_models; model; model = model->next)
        if (!strcasecmp(model->path, path)) {
            model->references++;
            return model;
        }
    model = MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->references = 1;
    model->next = cm_wow_doodad_models;
    cm_wow_doodad_models = model;
    data = FS_ReadFile(path, &size);
    if (!data && WowM2_ArchivePath(path, archive_path, sizeof(archive_path)))
        data = FS_ReadFile(archive_path, &size);
    model->status = WowM2_ParseCollision(data, size, &view);
    if (model->status == WOW_M2_COLLISION_MALFORMED) {
        fprintf(stderr, "OpenWoW collision: malformed M2 collision data %s\n", path);
        SAFE_DELETE(data, FS_FreeFile);
        return model;
    }
    if (model->status == WOW_M2_COLLISION_NONE) {
        FS_FreeFile(data);
        return model;
    }
    model->vertex_count = view.position_count;
    model->index_count = view.index_count;
    model->bounds = view.bounds;
    model->vertices = MemAlloc(sizeof(*model->vertices) * model->vertex_count);
    model->indices = MemAlloc(sizeof(*model->indices) * model->index_count);
    memcpy(model->vertices, view.positions, sizeof(*model->vertices) * model->vertex_count);
    memcpy(model->indices, view.indices, sizeof(*model->indices) * model->index_count);
    FS_FreeFile(data);
    return model;
}

static void CM_WowDoodadModelRelease(LPCMWOWDOODADMODEL model) {
    LPCMWOWDOODADMODEL *link = &cm_wow_doodad_models;

    if (!model || !model->references || --model->references) return;
    while (*link && *link != model) link = &(*link)->next;
    if (*link) *link = model->next;
    CM_WowDoodadModelFree(model);
}

void CM_WowCollisionReset(void) {
    while (cm_wow_collision_models) {
        LPCMWOWCOLLISIONMODEL next = cm_wow_collision_models->next;
        CM_WowCollisionModelFree(cm_wow_collision_models);
        cm_wow_collision_models = next;
    }
    while (cm_wow_doodad_models) {
        LPCMWOWDOODADMODEL next = cm_wow_doodad_models->next;
        CM_WowDoodadModelFree(cm_wow_doodad_models);
        cm_wow_doodad_models = next;
    }
}

void CM_WowCollisionTileFree(LPCMWOWCOLLISIONTILE tile) {
    if (!tile) return;
    FOR_LOOP(i, tile->instance_count) SAFE_DELETE(tile->instances[i].group_bounds, MemFree);
    SAFE_DELETE(tile->instances, MemFree);
    SAFE_DELETE(tile->doodad_instances, MemFree);
    SAFE_DELETE(tile->doodad_chunk_refs, MemFree);
    FOR_LOOP(i, tile->doodad_model_count) CM_WowDoodadModelRelease(tile->doodad_models[i]);
    SAFE_DELETE(tile->doodad_models, MemFree);
    memset(tile, 0, sizeof(*tile));
}

static LPCMWOWDOODADMODEL CM_WowDoodadTileModel(LPCMWOWCOLLISIONTILE tile, LPCSTR path) {
    FOR_LOOP(i, tile->doodad_model_count)
        if (!strcasecmp(tile->doodad_models[i]->path, path)) return tile->doodad_models[i];
    tile->doodad_models[tile->doodad_model_count] = CM_WowDoodadModelAcquire(path);
    return tile->doodad_models[tile->doodad_model_count++];
}

static DWORD CM_WowDoodadChunk(LPCCMWOWCOLLISIONTILE tile, LPCWOWBOX bounds) {
    FLOAT center_x = (bounds->min.x + bounds->max.x) * 0.5f;
    FLOAT center_y = (bounds->min.y + bounds->max.y) * 0.5f;
    int row = (int)floorf((tile->doodad_tile_max.x - center_x) / CM_WOW_COLLISION_CHUNK_SIZE);
    int col = (int)floorf((tile->doodad_tile_max.y - center_y) / CM_WOW_COLLISION_CHUNK_SIZE);

    row = MAX(0, MIN(15, row)); col = MAX(0, MIN(15, col));
    return (DWORD)(row * 16 + col);
}

/* Center bucketing plus the tile's maximum mesh extent visits every overlapping instance exactly once. */
static void CM_WowDoodadBuildPartition(LPCMWOWCOLLISIONTILE tile) {
    DWORD counts[256] = { 0 };
    DWORD cursors[256];

    FOR_LOOP(i, tile->doodad_instance_count) counts[CM_WowDoodadChunk(tile, &tile->doodad_instances[i].bounds)]++;
    FOR_LOOP(i, 256) tile->doodad_chunk_offsets[i + 1] = tile->doodad_chunk_offsets[i] + counts[i];
    if (!tile->doodad_chunk_offsets[256]) return;
    tile->doodad_chunk_refs = MemAlloc(sizeof(*tile->doodad_chunk_refs) * tile->doodad_chunk_offsets[256]);
    memcpy(cursors, tile->doodad_chunk_offsets, sizeof(cursors));
    FOR_LOOP(i, tile->doodad_instance_count) {
        DWORD chunk = CM_WowDoodadChunk(tile, &tile->doodad_instances[i].bounds);

        tile->doodad_chunk_refs[cursors[chunk]++] = i;
    }
}

/* ADT WMO and doodad references are loaded with terrain so every static query shares one archive view. */
BOOL CM_WowCollisionTileLoad(LPCMWOWCOLLISIONTILE tile, BYTE const *data, DWORD size) {
    WOWADTOBJECTVIEW adt;
    DWORD filedata_count = 0;

    if (!tile || !data) return false;
    CM_WowCollisionTileFree(tile);
    if (!WowAdt_ParseObjects(data, size, &adt)) {
        fprintf(stderr, "OpenWoW collision: malformed ADT object chunks\n");
        return false;
    }
    tile->doodad_grid_valid = adt.tile_grid_valid;
    tile->doodad_tile_max = adt.tile_max;
    if (adt.wmo_definition_count && (!adt.wmo_names || !adt.wmo_name_offsets)) {
        fprintf(stderr, "OpenWoW collision: ADT MODF lacks MWMO/MWID names\n");
        return false;
    }
    if (adt.wmo_definition_count) {
        tile->instances = MemAlloc(sizeof(*tile->instances) * adt.wmo_definition_count);
        memset(tile->instances, 0, sizeof(*tile->instances) * adt.wmo_definition_count);
        FOR_LOOP(i, adt.wmo_definition_count) {
            LPCWOWMAPOBJDEF definition = adt.wmo_definitions + i;
            WOWWMOREFERENCE reference = {
                .blob = adt.wmo_names, .blob_size = adt.wmo_names_size,
                .offsets = adt.wmo_name_offsets, .offset_count = adt.wmo_name_offset_count,
                .index = definition->name_id,
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
                    WowWmo_TransformBox(&model->groups[group_index].bounds, &instance->matrix);
                if (!CM_WowBoxValid(&instance->group_bounds[group_index])) continue;
                CM_WowAddBoxPoint(&instance->bounds, (LPCVECTOR3)&instance->group_bounds[group_index].min);
                CM_WowAddBoxPoint(&instance->bounds, (LPCVECTOR3)&instance->group_bounds[group_index].max);
            }
        }
    }
    if (!adt.doodad_definition_count) return true;
    if (!adt.doodad_names || !adt.doodad_name_offsets || !tile->doodad_grid_valid) {
        fprintf(stderr, "OpenWoW collision: ADT MDDF lacks MMDX/MMID names or MCNK grid\n");
        CM_WowCollisionTileFree(tile);
        return false;
    }
    tile->doodad_models = MemAlloc(sizeof(*tile->doodad_models) * adt.doodad_definition_count);
    tile->doodad_instances = MemAlloc(sizeof(*tile->doodad_instances) * adt.doodad_definition_count);
    memset(tile->doodad_instances, 0, sizeof(*tile->doodad_instances) * adt.doodad_definition_count);
    FOR_LOOP(i, adt.doodad_definition_count) {
        LPCWOWDOODADDEF definition = adt.doodad_definitions + i;
        WOWWMOREFERENCE reference = {
            .blob = adt.doodad_names, .blob_size = adt.doodad_names_size,
            .offsets = adt.doodad_name_offsets, .offset_count = adt.doodad_name_offset_count,
            .index = definition->name_id,
        };
        LPCSTR path;
        LPCMWOWDOODADMODEL model;
        LPCMWOWDOODADINSTANCE instance;

        /* MDDF 0x40 stores a FileDataID instead of an MMDX index and cannot use this path contract. */
        if (definition->flags & 0x40) {
            filedata_count++;
            continue;
        }
        path = WowWmo_StringRef(&reference);
        if (!path) {
            fprintf(stderr, "OpenWoW collision: ADT MDDF %u has invalid M2 name %u\n",
                    (unsigned)i, (unsigned)definition->name_id);
            continue;
        }
        model = CM_WowDoodadTileModel(tile, path);
        if (model->status != WOW_M2_COLLISION_VALID) continue;
        instance = &tile->doodad_instances[tile->doodad_instance_count++];
        instance->model = model;
        WowM2_DoodadMatrix(definition, &instance->matrix);
        instance->bounds = WowWmo_TransformBox(&model->bounds, &instance->matrix);
        tile->doodad_max_extent.x = MAX(tile->doodad_max_extent.x,
            (instance->bounds.max.x - instance->bounds.min.x) * 0.5f);
        tile->doodad_max_extent.y = MAX(tile->doodad_max_extent.y,
            (instance->bounds.max.y - instance->bounds.min.y) * 0.5f);
    }
    if (filedata_count)
        fprintf(stderr, "OpenWoW collision: ADT skipped %u FileDataID doodads without path references\n",
                (unsigned)filedata_count);
    CM_WowDoodadBuildPartition(tile);
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

static BOOL CM_WowDoodadChunkRange(LPCCMWOWCOLLISIONTILE tile, LPCWOWBOX bounds,
                                   int range[4]) {
    FLOAT tile_min_x = tile->doodad_tile_max.x - WOW_WMO_ADT_SIZE;
    FLOAT tile_min_y = tile->doodad_tile_max.y - WOW_WMO_ADT_SIZE;
    WOWBOX expanded = *bounds;

    expanded.min.x -= tile->doodad_max_extent.x; expanded.max.x += tile->doodad_max_extent.x;
    expanded.min.y -= tile->doodad_max_extent.y; expanded.max.y += tile->doodad_max_extent.y;
    if (!tile->doodad_grid_valid || expanded.max.x < tile_min_x ||
        expanded.min.x > tile->doodad_tile_max.x || expanded.max.y < tile_min_y ||
        expanded.min.y > tile->doodad_tile_max.y)
        return false;
    range[0] = MAX(0, (int)floorf((tile->doodad_tile_max.x - expanded.max.x) /
                                   CM_WOW_COLLISION_CHUNK_SIZE));
    range[1] = MIN(15, (int)floorf((tile->doodad_tile_max.x - expanded.min.x) /
                                    CM_WOW_COLLISION_CHUNK_SIZE));
    range[2] = MAX(0, (int)floorf((tile->doodad_tile_max.y - expanded.max.y) /
                                   CM_WOW_COLLISION_CHUNK_SIZE));
    range[3] = MIN(15, (int)floorf((tile->doodad_tile_max.y - expanded.min.y) /
                                    CM_WOW_COLLISION_CHUNK_SIZE));
    return range[0] <= range[1] && range[2] <= range[3];
}

static BOOL CM_WowDoodadTriangle(LPCCMWOWDOODADINSTANCE instance, DWORD face,
                                 LPCMWOWTRIANGLE triangle) {
    DWORD first = face * 3;
    WORD ia, ib, ic;

    if (first + 2 >= instance->model->index_count) return false;
    ia = instance->model->indices[first];
    ib = instance->model->indices[first + 1];
    ic = instance->model->indices[first + 2];
    if (ia >= instance->model->vertex_count || ib >= instance->model->vertex_count ||
        ic >= instance->model->vertex_count)
        return false;
    *triangle = (CMWOWTRIANGLE){
        Matrix4_multiply_vector3(&instance->matrix, instance->model->vertices + ia),
        Matrix4_multiply_vector3(&instance->matrix, instance->model->vertices + ib),
        Matrix4_multiply_vector3(&instance->matrix, instance->model->vertices + ic),
    };
    return true;
}

/* Chunk-center refs bound candidate work while exact transformed collision triangles remain authoritative. */
static BOOL CM_WowCollisionSweepDoodads(LPCCMWOWDOODADSWEEP sweep) {
    LPCCMWOWCOLLISIONTILE tile = sweep->tile;
    LPCWOWSWEEPQUERY query = sweep->query;
    int range[4];
    BOOL hit = false;

    if (!tile->doodad_instance_count ||
        !CM_WowDoodadChunkRange(tile, sweep->sweep_bounds, range)) return false;
    for (int row = range[0]; row <= range[1]; row++)
        for (int col = range[2]; col <= range[3]; col++) {
            DWORD chunk = (DWORD)(row * 16 + col);

            for (DWORD ref = tile->doodad_chunk_offsets[chunk];
                 ref < tile->doodad_chunk_offsets[chunk + 1]; ref++) {
                LPCCMWOWDOODADINSTANCE instance = tile->doodad_instances + tile->doodad_chunk_refs[ref];
                BOOL instance_hit = false;

                CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_DOODAD_INSTANCE_TESTS, 1);
                if (!CM_WowBoxesIntersect(&instance->bounds, sweep->sweep_bounds)) continue;
                CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_DOODAD_BROADPHASE_CANDIDATES, 1);
                FOR_LOOP(face, instance->model->index_count / 3) {
                    CMWOWTRIANGLE triangle;
                    VECTOR3 normal;

                    CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_DOODAD_TRIANGLE_TESTS, 1);
                    CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_TRIANGLE_TESTS, 1);
                    if (!CM_WowDoodadTriangle(instance, face, &triangle)) continue;
                    normal = CM_WowTriangleNormal(&triangle);
                    if (query->mode == WOW_SWEEP_MOVEMENT &&
                        fabsf(normal.z) >= CM_WOW_WALKABLE_NORMAL_Z)
                        continue;
                    instance_hit |= CM_WowCollisionSweepTriangle(&(CMWOWTRIANGLESWEEP){
                        .query = query, .triangle = triangle, .surface = WOW_SURFACE_DOODAD,
                    }, sweep->result);
                }
                if (instance_hit) CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_DOODAD_HITS, 1);
                hit |= instance_hit;
            }
        }
    return hit;
}

BOOL CM_WowCollisionSweepTile(LPCCMWOWCOLLISIONTILE tile,
                              LPCWOWSWEEPQUERY query,
                              LPWOWSWEEPRESULT result) {
    WOWBOX sweep_bounds = CM_WowCollisionSweepBounds(query);
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

                CMWOWGROUPFACE source = { group, face, &instance->matrix };

                CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_TRIANGLE_TESTS, 1);
                if (!CM_WowGroupTriangle(&source, &triangle)) continue;
                triangle_normal = CM_WowTriangleNormal(&triangle);
                if (query->mode == WOW_SWEEP_MOVEMENT &&
                    fabsf(triangle_normal.z) >= CM_WOW_WALKABLE_NORMAL_Z) continue;
                hit |= CM_WowCollisionSweepTriangle(&(CMWOWTRIANGLESWEEP){
                    .query = query, .triangle = triangle, .surface = WOW_SURFACE_WMO,
                }, result);
            }
        }
    }
    hit |= CM_WowCollisionSweepDoodads(&(CMWOWDOODADSWEEP){
        .tile = tile, .query = query, .result = result, .sweep_bounds = &sweep_bounds,
    });
    return hit;
}
