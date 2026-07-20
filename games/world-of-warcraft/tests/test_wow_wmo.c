#include "test_framework.h"

#include <string.h>

#include "common/wow_wmo_format.h"

int _tests_run;
int _tests_failed;

typedef struct {
    BYTE data[2048];
    DWORD size;
} TESTBLOB;

typedef struct {
    LPBYTE group;
    LPDWORD offset;
    LPCSTR tag;
    LPCVOID data;
    DWORD size;
} TESTSUBCHUNK;

static void put32(LPBYTE out, DWORD value) {
    out[0] = (BYTE)value; out[1] = (BYTE)(value >> 8);
    out[2] = (BYTE)(value >> 16); out[3] = (BYTE)(value >> 24);
}

static LPBYTE add_chunk(TESTBLOB *blob, LPCSTR tag, DWORD size) {
    LPBYTE chunk = blob->data + blob->size + 8;

    memcpy(blob->data + blob->size, tag, 4);
    put32(blob->data + blob->size + 4, size);
    memset(chunk, 0, size);
    blob->size += 8 + size;
    return chunk;
}

static void add_subchunk(TESTSUBCHUNK const *chunk) {
    memcpy(chunk->group + *chunk->offset, chunk->tag, 4);
    put32(chunk->group + *chunk->offset + 4, chunk->size);
    memcpy(chunk->group + *chunk->offset + 8, chunk->data, chunk->size);
    *chunk->offset += 8 + chunk->size;
}

static TESTBLOB make_root(void) {
    TESTBLOB blob = { 0 };
    LPBYTE chunk;
    BYTE materials[64] = { 0 };

    chunk = add_chunk(&blob, "REVM", 4);
    put32(chunk, 17);
    chunk = add_chunk(&blob, "DHOM", 64);
    put32(chunk + 4, 1);
    memcpy(add_chunk(&blob, "XTOM", 17), "Test\\Stone.blp", 15);
    put32(materials + 0x0c, 0);
    memcpy(add_chunk(&blob, "TMOM", sizeof(materials)), materials, sizeof(materials));
    return blob;
}

static TESTBLOB make_group(void) {
    TESTBLOB blob = { 0 };
    WOWWMOPOLY polygons[3] = { { 0x20, 0 }, { 0x08, 0xff }, { 0x20, 0 } };
    WORD indices[9] = { 0, 1, 2, 0, 2, 3, 1, 4, 2 };
    WOWVEC3 vertices[5] = {
        { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 0, 4, 0 }, { 4, 0, 4 },
    };
    WOWVEC2 uvs[5] = { 0 };
    WOWWMOBATCHDEF batch = { .first_index = 0, .num_indices = 9 };
    WOWWMOBSPNODE node = { .plane_type = 4, .num_faces = 3, .first_face = 0 };
    WORD refs[3] = { 0, 1, 2 };
    DWORD sub = WOW_WMO_GROUP_HEADER_SIZE;
    DWORD group_size = WOW_WMO_GROUP_HEADER_SIZE +
        8 + sizeof(polygons) + 8 + sizeof(indices) + 8 + sizeof(vertices) +
        8 + sizeof(uvs) + 8 + sizeof(batch) + 8 + sizeof(node) + 8 + sizeof(refs);
    LPBYTE group;

    memcpy(add_chunk(&blob, "REVM", 4), "\x11\0\0\0", 4);
    group = add_chunk(&blob, "PGOM", group_size);
    put32(group + 8, 9);
    memcpy(group + 12, &(WOWVEC3){ 0, 0, 0 }, sizeof(WOWVEC3));
    memcpy(group + 24, &(WOWVEC3){ 4, 4, 4 }, sizeof(WOWVEC3));
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "YPOM", polygons, sizeof(polygons) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "IVOM", indices, sizeof(indices) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "TVOM", vertices, sizeof(vertices) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "VTOM", uvs, sizeof(uvs) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "ABOM", &batch, sizeof(batch) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "NBOM", &node, sizeof(node) });
    add_subchunk(&(TESTSUBCHUNK){ group, &sub, "RBOM", refs, sizeof(refs) });
    return blob;
}

static void test_root_parser_reads_groups_materials_and_bounded_strings(void) {
    TESTBLOB blob = make_root();
    WOWWMOROOTVIEW view;

    ASSERT(WowWmo_ParseRoot(blob.data, blob.size, &view));
    ASSERT_EQ_INT((int)view.group_count, 1);
    ASSERT_EQ_INT((int)view.material_count, 1);
    ASSERT_STR_EQ(WowWmo_StringAt(view.textures, view.texture_size, 0), "Test\\Stone.blp");
    ASSERT_NULL(WowWmo_StringAt(view.textures, view.texture_size, view.texture_size));
}

static void test_group_parser_exposes_draw_and_bsp_collision_chunks(void) {
    TESTBLOB blob = make_group();
    WOWWMOGROUPVIEW view;

    ASSERT(WowWmo_ParseGroup(blob.data, blob.size, &view));
    ASSERT_EQ_INT((int)view.polygon_count, 3);
    ASSERT_EQ_INT((int)view.index_count, 9);
    ASSERT_EQ_INT((int)view.vertex_count, 5);
    ASSERT_EQ_INT((int)view.batch_count, 1);
    ASSERT_EQ_INT((int)view.bsp_node_count, 1);
    ASSERT_EQ_INT((int)view.bsp_ref_count, 3);
    ASSERT_EQ_INT((int)view.bsp_nodes[0].plane_type, 4);
    ASSERT_EQ_INT((int)view.bsp_refs[2], 2);
}

static void test_parser_rejects_truncated_chunks(void) {
    TESTBLOB root = make_root();
    TESTBLOB group = make_group();
    WOWWMOROOTVIEW root_view;
    WOWWMOGROUPVIEW group_view;

    ASSERT(!WowWmo_ParseRoot(root.data, root.size - 1, &root_view));
    ASSERT(!WowWmo_ParseGroup(group.data, group.size - 1, &group_view));
}

static void test_group_path_and_modf_transform_match_renderer_coordinates(void) {
    WOWMAPOBJDEF definition = {
        .position = { WOW_WMO_WORLD_OFFSET - 200.0f, 20.0f, WOW_WMO_WORLD_OFFSET - 100.0f },
        .rotation = { 0.0f, 270.0f, 90.0f },
        .scale = 1024,
    };
    MATRIX4 matrix;
    VECTOR3 local = { 2.0f, 3.0f, 4.0f };
    VECTOR3 world;
    PATHSTR path;

    WowWmo_GroupPath(&(WOWWMOGROUPPATH){
        .root = "World\\Test.wmo", .group = 7, .out = path, .out_size = sizeof(path),
    });
    ASSERT_STR_EQ(path, "World\\Test_007.wmo");
    WowWmo_InstanceMatrix(&definition, &matrix);
    world = Matrix4_multiply_vector3(&matrix, &local);
    ASSERT_EQ_FLOAT(world.x, 104.0f, 0.001f);
    ASSERT_EQ_FLOAT(world.y, 202.0f, 0.001f);
    ASSERT_EQ_FLOAT(world.z, 23.0f, 0.001f);
}

int main(void) {
    RUN_TEST(test_root_parser_reads_groups_materials_and_bounded_strings);
    RUN_TEST(test_group_parser_exposes_draw_and_bsp_collision_chunks);
    RUN_TEST(test_parser_rejects_truncated_chunks);
    RUN_TEST(test_group_path_and_modf_transform_match_renderer_coordinates);
    TEST_RESULTS();
}
