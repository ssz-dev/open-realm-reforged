#include "test_framework.h"
#include "renderer/wow/r_wowmap.h"
#include "renderer/wow/r_wowmap_minimap.h"

int _tests_run = 0;
int _tests_failed = 0;

static DWORD submit_calls;
static VECTOR2 submitted_mins;
static VECTOR2 submitted_maxs;
static LPCTEXTURE submitted_texture;
static LPCSHADER submitted_shader;
static COLOR32 submitted_color;

void R_RenderRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    submit_calls++;
    submitted_mins = *mins;
    submitted_maxs = *maxs;
    submitted_texture = texture;
    submitted_shader = shader;
    submitted_color = color;
}

static void reset_submit(void) {
    submit_calls = 0;
    submitted_mins = (VECTOR2){ 0 };
    submitted_maxs = (VECTOR2){ 0 };
    submitted_texture = NULL;
    submitted_shader = NULL;
    submitted_color = (COLOR32){ 0 };
}

static void test_round_splat_submits_terrain_bounds(void) {
    VECTOR2 position = { 42.5f, -17.0f };
    LPCTEXTURE texture = (LPCTEXTURE)(uintptr_t)1;
    LPCSHADER shader = (LPCSHADER)(uintptr_t)2;
    COLOR32 color = { 10, 20, 30, 40 };

    reset_submit();
    R_RenderSplat(&position, 3.5f, texture, shader, color);

    ASSERT_EQ_INT(submit_calls, 1);
    ASSERT_EQ_FLOAT(submitted_mins.x, 39.0f, 0.001f);
    ASSERT_EQ_FLOAT(submitted_mins.y, -20.5f, 0.001f);
    ASSERT_EQ_FLOAT(submitted_maxs.x, 46.0f, 0.001f);
    ASSERT_EQ_FLOAT(submitted_maxs.y, -13.5f, 0.001f);
    ASSERT(submitted_texture == texture);
    ASSERT(submitted_shader == shader);
    ASSERT(!memcmp(&submitted_color, &color, sizeof(color)));
}

static void test_minimap_translation_resolves_artificial_tile(void) {
    static LPCSTR const translations =
        "dir: Azeroth\n"
        "Azeroth\\map28_31.blp\tother.blp\n"
        "Azeroth\\map29_31.blp\t303d8fede1f036353bfe99b85419eb21.blp\r\n";
    PATHSTR path;

    ASSERT(Wow_FindMinimapTilePath(translations, "Azeroth", 29, 31, path, sizeof(path)));
    ASSERT_STR_EQ(path, "textures\\Minimap\\303d8fede1f036353bfe99b85419eb21.blp");
    ASSERT(!Wow_FindMinimapTilePath(translations, "Azeroth", 30, 31, path, sizeof(path)));
    ASSERT(!Wow_FindMinimapTilePath(NULL, "Azeroth", 29, 31, path, sizeof(path)));
}

static void test_minimap_center_moves_continuously_inside_tile(void) {
    VECTOR2 a = Wow_MinimapTileCenter(523.0f, 1589.0f, 29, 31);
    VECTOR2 b = Wow_MinimapTileCenter(530.0f, 1589.0f, 29, 31);

    ASSERT_EQ_FLOAT(a.x - b.x, 7.0f / WOW_ADT_SIZE, 0.0001f);
    ASSERT_EQ_FLOAT(a.y, b.y, 0.0001f);
    ASSERT(a.x >= 0.0f && a.x <= 1.0f);
    ASSERT(a.y >= 0.0f && a.y <= 1.0f);
}

int main(void) {
    printf("OpenWoW renderer tests\n");
    RUN_TEST(test_round_splat_submits_terrain_bounds);
    RUN_TEST(test_minimap_translation_resolves_artificial_tile);
    RUN_TEST(test_minimap_center_moves_continuously_inside_tile);
    TEST_RESULTS();
}
