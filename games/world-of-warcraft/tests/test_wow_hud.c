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
static capturedFrame_t captured[128];
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
    capturedFrame_t const *strike;
    capturedFrame_t const *heavy;
    capturedFrame_t const *throw_action;
    BOOL found_zone = false;
    BOOL found_level = false, found_health = false, found_power = false, found_xp = false;
    BOOL found_health_text = false, found_power_text = false, found_xp_text = false;
    BOOL found_hotkey1 = false, found_hotkey2 = false, found_hotkey3 = false;
    BOOL found_rage_cost = false, found_cooldown = false, found_feedback = false, found_disabled = false;

    reset_state();
    memset(&wc, 0, sizeof(wc));
    memset(&ent, 0, sizeof(ent));
    ent.client = &wc.client;
    wc.client.ps.name = "Tester";
    wc.client.ps.stats[WOW_STAT_HEALTH] = 80;
    wc.client.ps.stats[WOW_STAT_HEALTH_MAX] = 100;
    wc.client.ps.stats[WOW_STAT_POWER] = 25;
    wc.client.ps.stats[WOW_STAT_POWER_MAX] = 100;
    wc.client.ps.stats[WOW_STAT_LEVEL] = 2;
    wc.client.ps.stats[WOW_STAT_XP] = 100;
    wc.client.ps.stats[WOW_STAT_XP_MAX] = 900;
    snprintf(wc.actions[0].icon, sizeof(wc.actions[0].icon), "%s",
             "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    snprintf(wc.actions[0].name, sizeof(wc.actions[0].name), "%s", "Strike");
    wc.actions[0].count = 1;
    wc.actions[0].flags = WOW_ACTION_DISABLED_NO_TARGET;
    snprintf(wc.actions[1].icon, sizeof(wc.actions[1].icon), "%s",
             "Interface\\Icons\\Ability_Warrior_Charge.blp");
    snprintf(wc.actions[1].name, sizeof(wc.actions[1].name), "%s", "Heavy Strike");
    wc.actions[1].count = 1;
    wc.actions[1].rage_cost = BZ_WOW_HEAVY_STRIKE_RAGE;
    wc.actions[1].cooldown = 2;
    wc.actions[1].flags = WOW_ACTION_DISABLED_COOLDOWN;
    snprintf(wc.actions[2].icon, sizeof(wc.actions[2].icon), "%s",
             "Interface\\Icons\\Spell_Fire_FireBolt02.blp");
    snprintf(wc.actions[2].name, sizeof(wc.actions[2].name), "%s", "Throw");
    wc.actions[2].count = 1;
    wc.combat_message.type = WOW_COMBAT_MESSAGE_NO_RAGE;
    wc.combat_message.time = 1000;
    snprintf(wc.combat_message.text, sizeof(wc.combat_message.text), "%s", "Not enough Rage");
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
    strike = find_frame(FT_TEXTURE, "Interface\\Icons\\Ability_Warrior_Cleave.blp", "wow_action 0");
    heavy = find_frame(FT_TEXTURE, "Interface\\Icons\\Ability_Warrior_Charge.blp", "wow_action 1");
    throw_action = find_frame(FT_TEXTURE, "Interface\\Icons\\Spell_Fire_FireBolt02.blp", "wow_action 2");
    ASSERT_NOT_NULL(strike);
    ASSERT_NOT_NULL(heavy);
    ASSERT_NOT_NULL(throw_action);
    ASSERT_STR_EQ(strike->tooltip, "Strike");
    ASSERT_STR_EQ(heavy->tooltip, "Heavy Strike");
    ASSERT_STR_EQ(throw_action->tooltip, "Throw");
    ASSERT_EQ_FLOAT(frame_x(minimap), 879.0f / 1024.0f, 0.0001f);
    ASSERT_EQ_FLOAT(frame_y(minimap), 30.0f / 768.0f, 0.0001f);
    ASSERT_EQ_FLOAT(minimap->frame.size.width, 128.0f / 1024.0f, 0.0001f);
    ASSERT_EQ_FLOAT(minimap->frame.size.height, 128.0f / 768.0f, 0.0001f);
    ASSERT(frame_y(quest) > frame_y(minimap) + minimap->frame.size.height);
    ASSERT_STR_EQ(quest->tooltip, "Open Quest Log");
    FOR_LOOP(i, num_captured) {
        if (!strcmp(captured[i].text, "The Sepulcher")) found_zone = true;
        if (!strcmp(captured[i].text, "Lvl 2")) found_level = true;
        if (!strcmp(captured[i].text, "80 / 100")) found_health_text = true;
        if (!strcmp(captured[i].text, "Rage 25 / 100")) found_power_text = true;
        if (!strcmp(captured[i].text, "XP 100 / 900")) found_xp_text = true;
        if (!strcmp(captured[i].text, "1")) found_hotkey1 = true;
        if (!strcmp(captured[i].text, "2")) found_hotkey2 = true;
        if (!strcmp(captured[i].text, "3")) found_hotkey3 = true;
        if (!strcmp(captured[i].text, "20R")) found_rage_cost = true;
        if (!strcmp(captured[i].text, "2s")) found_cooldown = true;
        if (!strcmp(captured[i].text, "Not enough Rage")) found_feedback = true;
        if (captured[i].frame.flags.type == FT_SIMPLESTATUSBAR && captured[i].frame.color.a == 135 &&
            captured[i].frame.color.r == 0 && captured[i].frame.color.g == 0 &&
            captured[i].frame.color.b == 0) found_disabled = true;
        if (captured[i].frame.flags.type != FT_SIMPLESTATUSBAR ||
            strcmp(captured[i].image, "Interface\\TargetingFrame\\UI-StatusBar.blp")) continue;
        if (captured[i].frame.color.g == 178) {
            found_health = true;
            ASSERT_EQ_FLOAT(captured[i].frame.value, 0.8f, 0.0001f);
        } else if (captured[i].frame.color.r == 180) {
            found_power = true;
            ASSERT_EQ_FLOAT(captured[i].frame.value, 0.25f, 0.0001f);
        } else if (captured[i].frame.color.b == 210) {
            found_xp = true;
            ASSERT_EQ_FLOAT(captured[i].frame.value, 100.0f / 900.0f, 0.0001f);
        }
    }
    ASSERT(found_zone);
    ASSERT(found_level);
    ASSERT(found_health && found_health_text);
    ASSERT(found_power && found_power_text);
    ASSERT(found_xp && found_xp_text);
    ASSERT(found_hotkey1 && found_hotkey2 && found_hotkey3);
    ASSERT(found_rage_cost && found_cooldown && found_feedback && found_disabled);
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
