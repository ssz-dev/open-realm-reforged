#include "test_framework.h"
#include "renderer/wow/r_wowmap.h"

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

int main(void) {
    printf("OpenWoW renderer tests\n");
    RUN_TEST(test_round_splat_submits_terrain_bounds);
    TEST_RESULTS();
}
