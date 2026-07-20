#include "r_wowmap.h"

VECTOR3 Wow_ObjectPoint(wowVec3_t p) {
    return WowWmo_ObjectPoint(&p);
}

void Wow_InstanceMatrix(wowMapObjDef_t const *def, LPMATRIX4 matrix) { WowWmo_InstanceMatrix(def, matrix); }

BOOL Wow_LoadWmoGroup(wowWmoModel_t *model,
                             DWORD group_index,
                             LPTEXTURE const *materials,
                             DWORD material_count) {
    PATHSTR group_path;
    LPBYTE data = NULL;
    int size;
    WOWWMOGROUPVIEW view;
    BOX3 group_bounds = Wow_EmptyBounds();
    BOOL group_has_bounds = false;
    COLOR32 color = Wow_Color(127, 127, 127, 255);

    WowWmo_GroupPath(&(WOWWMOGROUPPATH){
        .root = model->path, .group = group_index, .out = group_path, .out_size = sizeof(group_path),
    });
    size = ri.FS_ReadFile(group_path, (void **)&data);
    if (size <= 0 || !data) {
        fprintf(stderr, "WoW WMO: missing group %s\n", group_path);
        return false;
    }

    if (!WowWmo_ParseGroup(data, (DWORD)size, &view)) {
        fprintf(stderr, "WoW WMO: malformed group %s\n", group_path);
        ri.FS_FreeFile(data);
        return false;
    }
    if (!view.vertices || !view.indices || !view.vertex_count || !view.index_count) {
        fprintf(stderr, "WoW WMO: group %s has no drawable geometry\n", group_path);
        ri.FS_FreeFile(data);
        return false;
    }

    if (view.batch_count) {
        FOR_LOOP(batch_index, view.batch_count) {
            LPCWOWWMOBATCHDEF batch = view.batches + batch_index;
            DWORD first_index = batch->first_index;
            DWORD num_indices = batch->num_indices;
            DWORD material_id = batch->material_id;
            VERTEX *out_vertices;
            DWORD out_count = 0;
            wowWmoBatch_t *out_batch;

            if (first_index >= view.index_count || first_index + num_indices > view.index_count || !num_indices) {
                continue;
            }
            out_vertices = ri.MemAlloc(sizeof(VERTEX) * num_indices);
            FOR_LOOP(i, num_indices) {
                WORD vertex_index = view.indices[first_index + i];
                wowVec3_t p;
                wowVec2_t uv = { 0.0f, 0.0f };
                if (vertex_index >= view.vertex_count) {
                    continue;
                }
                p = view.vertices[vertex_index];
                if (view.uvs && vertex_index < view.uv_count) {
                    uv = view.uvs[vertex_index];
                }
                out_vertices[out_count] = Wow_Vertex(p.x, p.y, p.z, uv.u, uv.v, color);
                Wow_AddBoundsPoint(&group_bounds, &out_vertices[out_count].position);
                group_has_bounds = true;
                out_count++;
            }
            if (!out_count) {
                ri.MemFree(out_vertices);
                continue;
            }

            out_batch = ri.MemAlloc(sizeof(*out_batch));
            memset(out_batch, 0, sizeof(*out_batch));
            out_batch->buffer = R_MakeVertexArrayObject(out_vertices, out_count);
            out_batch->num_vertices = out_count;
            out_batch->texture = material_id < material_count ? materials[material_id] : tr.texture[TEX_WHITE];
            out_batch->next = model->groups[group_index].batches;
            model->groups[group_index].batches = out_batch;
            wow_world.num_wmo_batches++;
            ri.MemFree(out_vertices);
        }
    } else {
        VERTEX *out_vertices = ri.MemAlloc(sizeof(VERTEX) * view.index_count);
        DWORD out_count = 0;
        FOR_LOOP(i, view.index_count) {
            WORD vertex_index = view.indices[i];
            DWORD poly_index = i / 3;
            DWORD material_id = 0;
            wowVec3_t p;
            wowVec2_t uv = { 0.0f, 0.0f };
            if (vertex_index >= view.vertex_count) {
                continue;
            }
            if (view.polygons && poly_index < view.polygon_count) {
                material_id = view.polygons[poly_index].material_id;
            }
            p = view.vertices[vertex_index];
            if (view.uvs && vertex_index < view.uv_count) {
                uv = view.uvs[vertex_index];
            }
            out_vertices[out_count] = Wow_Vertex(p.x, p.y, p.z, uv.u, uv.v, color);
            Wow_AddBoundsPoint(&group_bounds, &out_vertices[out_count].position);
            group_has_bounds = true;
            out_count++;
            if ((i % 3) == 2) {
                wowWmoBatch_t *out_batch = ri.MemAlloc(sizeof(*out_batch));
                memset(out_batch, 0, sizeof(*out_batch));
                out_batch->buffer = R_MakeVertexArrayObject(out_vertices + out_count - 3, 3);
                out_batch->num_vertices = 3;
                out_batch->texture = material_id < material_count ? materials[material_id] : tr.texture[TEX_WHITE];
                out_batch->next = model->groups[group_index].batches;
                model->groups[group_index].batches = out_batch;
                wow_world.num_wmo_batches++;
            }
        }
        ri.MemFree(out_vertices);
    }

    model->groups[group_index].bounds = group_bounds;
    model->groups[group_index].has_bounds = group_has_bounds;
    ri.FS_FreeFile(data);
    return true;
}

BOOL Wow_LoadWmoModel(wowWmoModel_t *model) {
    LPBYTE data = NULL;
    int size;
    WOWWMOROOTVIEW view;
    LPTEXTURE *materials = NULL;

    size = ri.FS_ReadFile(model->path, (void **)&data);
    if (size <= 0 || !data) {
        fprintf(stderr, "WoW WMO: missing root %s\n", model->path);
        return false;
    }

    if (!WowWmo_ParseRoot(data, (DWORD)size, &view)) {
        fprintf(stderr, "WoW WMO: malformed root %s\n", model->path);
        ri.FS_FreeFile(data);
        return false;
    }

    if (view.material_count) {
        materials = ri.MemAlloc(sizeof(*materials) * view.material_count);
        memset(materials, 0, sizeof(*materials) * view.material_count);
        FOR_LOOP(i, view.material_count) {
            DWORD texture_offset = WowWmo_Read32(view.materials + i * 64 + 0x0c);
            LPCSTR texture_path = WowWmo_StringAt(view.textures, view.texture_size, texture_offset);
            materials[i] = texture_path ? Wow_LoadTexture(texture_path) : tr.texture[TEX_WHITE];
        }
    }

    model->groups = ri.MemAlloc(sizeof(*model->groups) * view.group_count);
    memset(model->groups, 0, sizeof(*model->groups) * view.group_count);
    model->num_groups = view.group_count;
    FOR_LOOP(i, view.group_count) {
        Wow_LoadWmoGroup(model, i, materials, view.material_count);
    }

    if (materials) {
        ri.MemFree(materials);
    }
    ri.FS_FreeFile(data);
    model->loaded = true;
    return true;
}

wowWmoModel_t *Wow_GetWmoModel(LPCSTR path) {
    wowWmoModel_t *model;

    if (!path || !*path) {
        return NULL;
    }
    for (model = wow_world.wmo_models; model; model = model->next) {
        if (!strcasecmp(model->path, path)) {
            return model->loaded ? model : NULL;
        }
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->next = wow_world.wmo_models;
    wow_world.wmo_models = model;
    wow_world.num_wmo_models++;
    if (!Wow_LoadWmoModel(model)) {
        wow_world.num_missing_wmos++;
        return NULL;
    }
    return model;
}

void Wow_AddWmoInstance(LPCSTR path, wowMapObjDef_t const *def) {
    wowWmoModel_t *model = Wow_GetWmoModel(path);
    wowWmoInstance_t *instance;

    wow_world.num_wmos++;
    if (!model || !def) {
        return;
    }

    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->model = model;
    Wow_InstanceMatrix(def, &instance->matrix);
    instance->next = wow_world.wmos;
    wow_world.wmos = instance;
}

LPMODEL Wow_LoadDoodadModel(LPCSTR path) {
    wowDoodadModel_t *entry;

    if (!path || !*path) {
        return NULL;
    }
    for (entry = wow_world.doodad_models; entry; entry = entry->next) {
        if (!strcasecmp(entry->path, path)) {
            return entry->model;
        }
    }

    entry = ri.MemAlloc(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->model = R_LoadModel(path);
    entry->next = wow_world.doodad_models;
    wow_world.doodad_models = entry;
    wow_world.num_doodad_models++;
    if (!entry->model) {
        wow_world.num_missing_doodad_models++;
    }
    return entry->model;
}

int Wow_DoodadBucketIndex(float coord) {
    int index = (int)floorf((coord + WOW_WORLD_COORD_OFFSET) / WOW_DOODAD_BUCKET_SIZE);
    if (index < 0) {
        return 0;
    }
    if (index >= WOW_DOODAD_BUCKETS) {
        return WOW_DOODAD_BUCKETS - 1;
    }
    return index;
}

void Wow_BucketDoodadInstance(wowDoodadInstance_t *instance) {
    int bucket_x;
    int bucket_y;

    if (!instance) {
        return;
    }

    bucket_x = Wow_DoodadBucketIndex(instance->entity.origin.x);
    bucket_y = Wow_DoodadBucketIndex(instance->entity.origin.y);
    instance->bucket_next = wow_world.doodad_buckets[bucket_y][bucket_x];
    wow_world.doodad_buckets[bucket_y][bucket_x] = instance;
}

void Wow_AddDoodadInstance(LPCSTR model_path, wowDoodadDef_t const *def) {
    wowDoodadInstance_t *instance;
    LPMODEL model;

    if (!model_path || !*model_path || !def) {
        wow_world.num_missing_doodad_models++;
        return;
    }
    if (def->flags & 0x40) {
        wow_world.num_filedata_doodads++;
        return;
    }

    model = Wow_LoadDoodadModel(model_path);
    if (!model) {
        return;
    }

    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->entity.origin = Wow_ObjectPoint(def->position);
    instance->entity.rotation = (VECTOR3){ def->rotation.x, def->rotation.y, def->rotation.z };
    instance->entity.scale = def->scale / 1024.0f;
    instance->entity.model = model;
    instance->entity.radius = 32.0f;
    instance->entity.flags = RF_NO_SHADOW;
    instance->next = wow_world.doodads;
    wow_world.doodads = instance;
    Wow_BucketDoodadInstance(instance);
    wow_world.num_doodad_instances++;
}

void Wow_AddMarker(VERTEX *vertices, LPDWORD index, VECTOR3 p, float size, COLOR32 color) {
    VECTOR3 a = { p.x - size, p.y - size, p.z };
    VECTOR3 b = { p.x + size, p.y - size, p.z };
    VECTOR3 c = { p.x + size, p.y + size, p.z };
    VECTOR3 d = { p.x - size, p.y + size, p.z };
    VECTOR3 top = { p.x, p.y, p.z + size * 3.0f };
    vertices[(*index)++] = Wow_Vertex(a.x, a.y, a.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(b.x, b.y, b.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(b.x, b.y, b.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(c.x, c.y, c.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(c.x, c.y, c.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(d.x, d.y, d.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(d.x, d.y, d.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(a.x, a.y, a.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
}

VERTEX *Wow_AppendMarkers(VERTEX *old_vertices,
                                 LPDWORD old_count,
                                 BYTE const *chunk,
                                 DWORD size,
                                 BYTE const *name_blob,
                                 DWORD name_blob_size,
                                 DWORD const *name_offsets,
                                 DWORD name_offset_count,
                                 BOOL wmo) {
    DWORD record_size = wmo ? sizeof(wowMapObjDef_t) : sizeof(wowDoodadDef_t);
    DWORD count = size / record_size;
    DWORD new_count = *old_count + count * 12;
    VERTEX *vertices = ri.MemAlloc(sizeof(VERTEX) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(VERTEX) * *old_count);
        ri.MemFree(old_vertices);
    }

    FOR_LOOP(i, count) {
        VECTOR3 p;
        if (wmo) {
            wowMapObjDef_t const *def = (wowMapObjDef_t const *)(chunk + i * record_size);
            p = Wow_ObjectPoint(def->position);
            Wow_AddMarker(vertices, old_count, p, 18.0f, Wow_Color(90, 130, 255, 255));
            wow_world.num_wmos++;
        } else {
            wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * record_size);
            LPCSTR model_path = NULL;
            float model_scale = def->scale / 1024.0f;
            float radius = 0.0f;
            float marker_size;

            if (def->flags & 0x40) {
                wow_world.num_filedata_doodads++;
            } else {
                model_path = Wow_StringRefFromOffsets(name_blob, name_blob_size, name_offsets, name_offset_count, def->name_id);
                if (model_path) {
                    radius = Wow_LoadM2BoundsRadius(model_path);
                }
            }
            marker_size = radius > 0.0f ? radius * model_scale : model_scale * 8.0f;
            marker_size = MAX(5.0f, MIN(marker_size, 80.0f));
            p = Wow_ObjectPoint(def->position);
            Wow_AddMarker(vertices, old_count, p, marker_size, radius > 0.0f ? Wow_Color(90, 230, 130, 255) : Wow_Color(230, 210, 80, 255));
            wow_world.num_doodads++;
        }
    }

    return vertices;
}

VERTEX *Wow_AppendDoodadErrorMarkers(VERTEX *old_vertices,
                                            LPDWORD old_count,
                                            BYTE const *chunk,
                                            DWORD size) {
    DWORD count = size / sizeof(wowDoodadDef_t);
    DWORD new_count = *old_count + count * 12;
    VERTEX *vertices = ri.MemAlloc(sizeof(VERTEX) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(VERTEX) * *old_count);
        ri.MemFree(old_vertices);
    }

    FOR_LOOP(i, count) {
        wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * sizeof(*def));
        if (def->flags & 0x40) {
            wow_world.num_filedata_doodads++;
            continue;
        }
        Wow_AddMarker(vertices,
                      old_count,
                      Wow_ObjectPoint(def->position),
                      6.0f,
                      Wow_Color(255, 255, 255, 255));
        wow_world.num_doodad_instances++;
    }

    return vertices;
}
