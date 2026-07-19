#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

typedef struct {
    uiFrame_t frame;
    char image[MAX_PATHLEN];
    char text[256];
    char tooltip[128];
    char onclick[128];
} capturedFrame_t;

struct game_import gi;
static capturedFrame_t captured[64];
static char images[64][MAX_PATHLEN];
static DWORD num_captured;
static DWORD num_images;
static DWORD current_layer;
static DWORD byte_count;
static DWORD unicast_calls;

static int test_image_index(LPCSTR path) {
    FOR_LOOP(i, num_images)
        if (!strcmp(images[i], path)) return (int)i + 1;
    ASSERT(num_images < sizeof(images) / sizeof(images[0]));
    snprintf(images[num_images], sizeof(images[num_images]), "%s", path);
    return (int)++num_images;
}

static int test_font_index(LPCSTR path, DWORD size) {
    ASSERT_STR_EQ(path, "Fonts\\FRIZQT__.TTF");
    ASSERT(size > 0);
    return 1;
}

static void test_write(pfWriteType_t type, void const *value) {
    if (type == PF_BYTE) {
        DWORD byte = (DWORD)*(LONG const *)value;
        if (byte_count++ == 0) ASSERT_EQ_INT((int)byte, svc_layout);
        else current_layer = byte;
    } else if (type == PF_UIFRAME) {
        LPCUIFRAME source = value;
        capturedFrame_t *out;

        ASSERT(num_captured < sizeof(captured) / sizeof(captured[0]));
        out = &captured[num_captured++];
        out->frame = *source;
        if (source->tex.index > 0 && source->tex.index <= num_images)
            snprintf(out->image, sizeof(out->image), "%s", images[source->tex.index - 1]);
        snprintf(out->text, sizeof(out->text), "%s", source->text ? source->text : "");
        snprintf(out->tooltip, sizeof(out->tooltip), "%s", source->tooltip ? source->tooltip : "");
        snprintf(out->onclick, sizeof(out->onclick), "%s", source->onclick ? source->onclick : "");
        out->frame.text = out->text;
        out->frame.tooltip = out->tooltip;
        out->frame.onclick = out->onclick;
    }
}

static void test_unicast(LPEDICT ent) {
    ASSERT_NOT_NULL(ent);
    unicast_calls++;
}

static void reset_state(void) {
    memset(captured, 0, sizeof(captured));
    memset(images, 0, sizeof(images));
    memset(&gi, 0, sizeof(gi));
    num_captured = num_images = current_layer = byte_count = unicast_calls = 0;
    gi.ImageIndex = test_image_index;
    gi.FontIndex = test_font_index;
    gi.Write = test_write;
    gi.unicast = test_unicast;
}

static FLOAT frame_x(capturedFrame_t const *frame) {
    return frame->frame.points.x[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
}

static FLOAT frame_y(capturedFrame_t const *frame) {
    return -frame->frame.points.y[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
}

static capturedFrame_t const *find_frame(FRAMETYPE type, LPCSTR image, LPCSTR onclick) {
    FOR_LOOP(i, num_captured) {
        capturedFrame_t const *frame = &captured[i];
        if (frame->frame.flags.type != type) continue;
        if (image && strcmp(frame->image, image)) continue;
        if (onclick && strcmp(frame->onclick, onclick)) continue;
        return frame;
    }
    return NULL;
}

static void test_wow_hud_draws_clean_minimap_zone_header_and_quest_button(void) {
    wowClient_t wc;
    EDICT ent;
    capturedFrame_t const *minimap;
    capturedFrame_t const *border;
    capturedFrame_t const *quest;
    BOOL found_zone = false;

    reset_state();
    memset(&wc, 0, sizeof(wc));
    memset(&ent, 0, sizeof(ent));
    ent.client = &wc.client;
    wc.client.ps.name = "Tester";
    wc.client.ps.stats[WOW_STAT_LEVEL] = 1;
    snprintf(wc.zone_name, sizeof(wc.zone_name), "%s", "The Sepulcher");
    UI_WriteWowHud(&ent);

    ASSERT_EQ_INT((int)current_layer, LAYER_CONSOLE);
    ASSERT_EQ_INT((int)unicast_calls, 1);
    minimap = find_frame(FT_MINIMAP, NULL, NULL);
    border = find_frame(FT_TEXTURE, "Interface\\Minimap\\UI-Minimap-Border.blp", NULL);
    quest = find_frame(FT_TEXTURE, "Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp", "questlog toggle");
    ASSERT_NOT_NULL(minimap);
    ASSERT_NULL(border);
    ASSERT_NOT_NULL(quest);
    ASSERT_EQ_FLOAT(frame_x(minimap), 879.0f / 1024.0f, 0.0001f);
    ASSERT_EQ_FLOAT(frame_y(minimap), 30.0f / 768.0f, 0.0001f);
    ASSERT_EQ_FLOAT(minimap->frame.size.width, 128.0f / 1024.0f, 0.0001f);
    ASSERT_EQ_FLOAT(minimap->frame.size.height, 128.0f / 768.0f, 0.0001f);
    ASSERT(frame_y(quest) > frame_y(minimap) + minimap->frame.size.height);
    ASSERT_STR_EQ(quest->tooltip, "Open Quest Log");
    FOR_LOOP(i, num_captured)
        if (!strcmp(captured[i].text, "The Sepulcher")) found_zone = true;
    ASSERT(found_zone);
}

static void test_wow_quest_log_writes_classic_empty_state_and_close_action(void) {
    wowClient_t wc;
    EDICT ent;
    capturedFrame_t const *top_left;
    capturedFrame_t const *close;
    BOOL found_empty = false;

    reset_state();
    memset(&wc, 0, sizeof(wc));
    memset(&ent, 0, sizeof(ent));
    ent.client = &wc.client;
    UI_WriteWowQuestLog(&ent);

    ASSERT_EQ_INT((int)current_layer, LAYER_QUESTDIALOG);
    ASSERT_EQ_INT((int)unicast_calls, 1);
    top_left = find_frame(FT_TEXTURE, "Interface\\QuestFrame\\UI-QuestLog-TopLeft.blp", NULL);
    close = find_frame(FT_TEXTURE, "Interface\\Buttons\\UI-Panel-MinimizeButton-Up.blp", "questlog close");
    ASSERT_NOT_NULL(top_left);
    ASSERT_NOT_NULL(close);
    ASSERT_EQ_FLOAT(frame_x(top_left), 0.0f, 0.0001f);
    ASSERT_EQ_FLOAT(frame_y(top_left), 104.0f / 768.0f, 0.0001f);
    ASSERT_EQ_FLOAT(top_left->frame.size.width, 256.0f / 1024.0f, 0.0001f);
    ASSERT_EQ_FLOAT(top_left->frame.size.height, 256.0f / 768.0f, 0.0001f);
    FOR_LOOP(i, num_captured)
        if (!strcmp(captured[i].text, "No active quests")) found_empty = true;
    ASSERT(found_empty);

    reset_state();
    UI_HideWowQuestLog(&ent);
    ASSERT_EQ_INT((int)current_layer, LAYER_QUESTDIALOG);
    ASSERT_EQ_INT((int)num_captured, 0);
    ASSERT_EQ_INT((int)unicast_calls, 1);
}

int main(void) {
    RUN_TEST(test_wow_hud_draws_clean_minimap_zone_header_and_quest_button);
    RUN_TEST(test_wow_quest_log_writes_classic_empty_state_and_close_action);
    TEST_RESULTS();
}
