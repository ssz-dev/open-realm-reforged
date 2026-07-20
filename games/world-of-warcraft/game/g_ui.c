/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Reproduces the classic WoW 1.12 HUD layout (action bar, targeting frame,
 * minimap, copper) using the actual WoW assets and pixel positions from the
 * virtual 1024×768 canvas, exactly matching what ui.dll rendered before
 * in-game UI was moved server-side.
 */

#include "g_wow_local.h"

#define VW 1024.0f
#define VH 768.0f
#define PX(x) ((x) / VW)
#define PY(y) ((y) / VH)
#define PW(w) ((w) / VW)
#define PH(h) ((h) / VH)
#define HUD_FONT_SIZE 10
#define WOW_STATUS_BAR "Interface\\TargetingFrame\\UI-StatusBar.blp"

typedef struct {
    LPCSTR path;
    LPCSTR tooltip;
    LPCSTR onclick;
    RECT rect;
} wowHudButton_t;

typedef struct {
    FLOAT x, y;
    DWORD image_index;
    DWORD slot;
    wowHudIcon_t const *icon;
} WOWACTIONDRAW;
typedef WOWACTIONDRAW *LPWOWACTIONDRAW;
typedef WOWACTIONDRAW const *LPCWOWACTIONDRAW;

typedef struct {
    FLOAT x, y;
    DWORD image_index;
    DWORD slot;
    wowHudIcon_t const *icon;
} WOWITEMDRAW;
typedef WOWITEMDRAW *LPWOWITEMDRAW;
typedef WOWITEMDRAW const *LPCWOWITEMDRAW;

static DWORD ui_next_frame_number;

static void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative,
                             FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

static void UI_SetFrameRect(LPUIFRAME frame, FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

static void UI_WriteProxyFrame(LPUIFRAME frame, HANDLE data, DWORD data_size) {
    frame->number = ui_next_frame_number++;
    frame->parent = 0;
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    /* Set default full-UV only when caller left coords zeroed */
    if (!frame->tex.coord[1] && !frame->tex.coord[3]) {
        frame->tex.coord[1] = 0xff;
        frame->tex.coord[3] = 0xff;
    }
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(PF_UIFRAME, frame);
}

static void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text,
                              COLOR32 color, uiFontJustificationH_t align) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* Write an FT_TEXTURE frame with float-precision UV (supports l>r or t>b for flips). */
static void UI_WriteImageUV(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                            FLOAT l, FLOAT r, FLOAT t, FLOAT b, COLOR32 color) {
    uiFrame_t frame;
    uiTextureUV_t uv;

    memset(&frame, 0, sizeof(frame));
    memset(&uv, 0, sizeof(uv));
    frame.flags.type = FT_TEXTURE;
    frame.color = color;
    frame.tex.index = gi.ImageIndex(path);
    uv.l = l; uv.r = r; uv.t = t; uv.b = b;
    uv.color = color;
    uv.alphamode = BLEND_MODE_ALPHAKEY;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &uv, sizeof(uv));
}

static void UI_WriteImage(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    UI_WriteImageUV(path, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, color);
}

/* Clickable HUD art keeps the server command on the same frame used for hit testing. */
static void UI_WriteImageButton(wowHudButton_t const *button) {
    uiFrame_t frame;
    uiTextureUV_t uv;

    memset(&frame, 0, sizeof(frame));
    memset(&uv, 0, sizeof(uv));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(button->path);
    frame.tooltip = button->tooltip;
    frame.onclick = button->onclick;
    uv.r = uv.b = 1.0f;
    uv.color = COLOR32_WHITE;
    uv.alphamode = BLEND_MODE_ALPHAKEY;
    UI_SetFrameRect(&frame, button->rect.x, button->rect.y, button->rect.w, button->rect.h);
    UI_WriteProxyFrame(&frame, &uv, sizeof(uv));
}

static void UI_WriteStatusBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                              FLOAT value, FLOAT maxvalue, COLOR32 color) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR;
    frame.color = color;
    frame.tex.index = gi.ImageIndex(WOW_STATUS_BAR);
    frame.value = maxvalue > 0.0f ? MAX(0.0f, MIN(1.0f, value / maxvalue)) : 0.0f;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Solid panels use the archived status texture because texture slot zero is reserved for unresolved assets. */
static void UI_WriteColorRect(FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    UI_WriteStatusBar(x, y, w, h, 1.0f, 1.0f, color);
}

/* Health/resource bars use a dark inset and the same archived texture for the live fill. */
static void UI_WriteColorBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                             FLOAT value, FLOAT maxvalue,
                             COLOR32 fill_color) {
    UI_WriteColorRect(x, y, w, h, MAKE(COLOR32, 12, 10, 8, 220));
    UI_WriteStatusBar(x + PW(2), y + PH(2), w - PW(4), h - PH(4), value, maxvalue, fill_color);
}

/* Minimap: clean square viewport with a server-authored zone header. */
static void UI_WriteMinimapFrames(LPCSTR zone_name) {
    uiFrame_t minimap;

    UI_WriteColorRect(PX(879), PY(8), PW(128), PH(22), MAKE(COLOR32, 8, 12, 18, 220));
    UI_WriteTextFrame(PX(883), PY(11), PW(120), PH(16),
                      zone_name && *zone_name ? zone_name : "Azeroth",
                      MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);
    UI_WriteColorRect(PX(879), PY(30), PW(128), PH(128), MAKE(COLOR32, 5, 8, 12, 245));
    /* Minimap viewport — FT_MINIMAP; client calls DrawMinimap() for this rect. */
    memset(&minimap, 0, sizeof(minimap));
    minimap.flags.type = FT_MINIMAP;
    minimap.color = COLOR32_WHITE;
    UI_SetFrameRect(&minimap, PX(879), PY(30), PW(128), PH(128));
    UI_WriteProxyFrame(&minimap, NULL, 0);
}

/* Main action bar: four 256×53 strips + two end-caps from UI-MainMenuBar-Dwarf.blp */
static void UI_WriteActionBar(void) {
    static LPCSTR const bar = "Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp";
    static LPCSTR const cap = "Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp";
    /* Each strip covers a different vertical slice of the texture (v slices at 53/256 intervals) */
    static FLOAT const strips[4][4] = {
        /* {l, r, t, b}, screen x starts at 0 */
        { 0.0f, 1.0f, 0.79296875f, 1.0f },
        { 0.0f, 1.0f, 0.54296875f, 0.75f },
        { 0.0f, 1.0f, 0.29296875f, 0.5f },
        { 0.0f, 1.0f, 0.04296875f, 0.25f },
    };

    FOR_LOOP(i, 4)
        UI_WriteImageUV(bar,
                        PX((FLOAT)(i * 256)), PY(715), PW(256), PH(53),
                        strips[i][0], strips[i][1], strips[i][2], strips[i][3],
                        COLOR32_WHITE);

    /* Left end-cap (normal orientation) */
    UI_WriteImage(cap, PX(-96), PY(640), PW(128), PH(128), COLOR32_WHITE);
    /* Right end-cap (horizontally flipped: l=1, r=0) */
    UI_WriteImageUV(cap, PX(992), PY(640), PW(128), PH(128),
                    1.0f, 0.0f, 0.0f, 1.0f, COLOR32_WHITE);
}

/* Action slots expose only a server command and render server-owned cost, cooldown, and disabled state. */
static void UI_WriteActionButtonSlot(LPCWOWACTIONDRAW draw) {
    char command[32], count_buf[16], cost_buf[16], cooldown_buf[16], hotkey_buf[2];
    wowHudIcon_t const *icon = draw->icon;

    /* Slot frame */
    UI_WriteImage("Interface\\Buttons\\UI-Quickslot2.blp",
                  draw->x + PX(-14), draw->y + PY(-13), PW(64), PH(64), COLOR32_WHITE);
    /* Icon (may be 0 = empty slot, renderer draws nothing for index 0) */
    if (draw->image_index && icon) {
        uiFrame_t frame;

        snprintf(command, sizeof(command), "wow_action %u", (unsigned)draw->slot);
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_TEXTURE;
        frame.color = icon->flags ? MAKE(COLOR32, 110, 110, 110, 255) : COLOR32_WHITE;
        frame.tex.index = draw->image_index;
        frame.tex.coord[1] = 0xff;
        frame.tex.coord[3] = 0xff;
        frame.tooltip = icon->name;
        frame.onclick = command;
        UI_SetFrameRect(&frame, draw->x + PX(2), draw->y + PY(2), PW(32), PH(32));
        UI_WriteProxyFrame(&frame, NULL, 0);
        if (icon->flags)
            UI_WriteColorRect(draw->x + PX(2), draw->y + PY(2), PW(32), PH(32),
                              MAKE(COLOR32, 0, 0, 0, 135));
    }
    if (!icon) return;
    hotkey_buf[0] = draw->slot < 9 ? (char)('1' + draw->slot) : '0';
    hotkey_buf[1] = '\0';
    UI_WriteTextFrame(draw->x + PX(3), draw->y + PY(2), PW(12), PH(10),
                      hotkey_buf, COLOR32_WHITE, FONT_JUSTIFYLEFT);
    if (icon->rage_cost) {
        snprintf(cost_buf, sizeof(cost_buf), "%uR", (unsigned)icon->rage_cost);
        UI_WriteTextFrame(draw->x + PX(2), draw->y + PY(23), PW(31), PH(10),
                          cost_buf, MAKE(COLOR32, 255, 190, 80, 255), FONT_JUSTIFYRIGHT);
    } else if (icon->count > 1) {
        snprintf(count_buf, sizeof(count_buf), "%u", (unsigned)icon->count);
        UI_WriteTextFrame(draw->x + PX(2), draw->y + PY(23), PW(32), PH(10),
                          count_buf, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
    }
    if (icon->cooldown) {
        snprintf(cooldown_buf, sizeof(cooldown_buf), "%us", (unsigned)icon->cooldown);
        UI_WriteTextFrame(draw->x + PX(2), draw->y + PY(11), PW(32), PH(12),
                          cooldown_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);
    }
}

/* Inventory clicks send one existing server command; counts and tooltips remain derived HUD state. */
static void UI_WriteInventorySlot(LPCWOWITEMDRAW draw) {
    char command[32], count_buf[16];

    UI_WriteImage("Interface\\Buttons\\UI-Quickslot2.blp",
                  draw->x + PX(-14), draw->y + PY(-13), PW(64), PH(64), COLOR32_WHITE);
    if (!draw->image_index || !draw->icon) return;
    {
        uiFrame_t frame;

        snprintf(command, sizeof(command), "use_item %u", (unsigned)draw->slot);
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_TEXTURE;
        frame.color = COLOR32_WHITE;
        frame.tex.index = draw->image_index;
        frame.tex.coord[1] = frame.tex.coord[3] = 0xff;
        frame.tooltip = draw->icon->name;
        frame.onclick = command;
        UI_SetFrameRect(&frame, draw->x + PX(2), draw->y + PY(2), PW(32), PH(32));
        UI_WriteProxyFrame(&frame, NULL, 0);
    }
    if (draw->icon->count > 1) {
        snprintf(count_buf, sizeof(count_buf), "%u", (unsigned)draw->icon->count);
        UI_WriteTextFrame(draw->x + PX(2), draw->y + PY(23), PW(32), PH(10),
                          count_buf, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
    }
}

static void UI_WriteInventory(wowClient_t const *client) {
    UI_WriteTextFrame(PX(632), PY(638), PW(250), PH(14), "INVENTORY",
                      MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteTextFrame(PX(632), PY(652), PW(330), PH(14), client->equipment_text,
                      MAKE(COLOR32, 225, 210, 175, 255), FONT_JUSTIFYLEFT);
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        DWORD image = client->inventory[i].icon[0] ? gi.ImageIndex(client->inventory[i].icon) : 0;
        WOWITEMDRAW draw = {
            PX(632.0f + (FLOAT)i * 42.0f), PY(674), image, i,
            client->inventory[i].icon[0] ? &client->inventory[i] : NULL
        };

        UI_WriteInventorySlot(&draw);
    }
}

/* A nearby corpse exposes pickup through the same server-command hit-test path as other native HUD controls. */
static void UI_WriteLootPrompt(DWORD target) {
    char command[32];
    wowHudButton_t button = {
        .path = "Interface\\Icons\\INV_Misc_Bag_08.blp",
        .tooltip = "Collect corpse loot",
        .rect = { PX(542), PY(610), PW(40), PH(40) },
    };

    if (!target) return;
    snprintf(command, sizeof(command), "loot %u", (unsigned)target);
    button.onclick = command;
    UI_WriteImageButton(&button);
    UI_WriteTextFrame(PX(584), PY(620), PW(120), PH(18), "Loot corpse",
                      MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
}

/* Quest tracking and interaction share one derived snapshot without owning progress or reward decisions. */
static void UI_WriteQuestTracker(LPCWOWQUESTHUD quest) {
    FLOAT action_y;

    if (quest->tracker_visible) {
        UI_WriteColorRect(PX(24), PY(112), PW(280), PH(70), MAKE(COLOR32, 8, 12, 18, 215));
        UI_WriteTextFrame(PX(34), PY(120), PW(260), PH(16), quest->title,
                          MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
        UI_WriteTextFrame(PX(34), PY(141), PW(260), PH(15), quest->progress,
                          COLOR32_WHITE, FONT_JUSTIFYLEFT);
        UI_WriteTextFrame(PX(34), PY(160), PW(260), PH(14), quest->status,
                          MAKE(COLOR32, 225, 210, 175, 255), FONT_JUSTIFYLEFT);
    }
    if (!quest->action_command[0]) return;
    action_y = quest->tracker_visible ? PY(190) : PY(112);
    {
        wowHudButton_t button = {
            .path = "Interface\\Buttons\\UI-DialogBox-Button-Up.blp",
            .tooltip = quest->action_label,
            .onclick = quest->action_command,
            .rect = { PX(34), action_y + PY(24), PW(128), PH(32) },
        };

        UI_WriteColorRect(PX(24), action_y, PW(280), PH(66), MAKE(COLOR32, 8, 12, 18, 225));
        UI_WriteTextFrame(PX(34), action_y + PY(6), PW(260), PH(16), quest->title,
                          MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
        UI_WriteImageButton(&button);
        UI_WriteTextFrame(PX(42), action_y + PY(32), PW(112), PH(14), quest->action_label,
                          COLOR32_WHITE, FONT_JUSTIFYCENTER);
    }
}

/* Targeting frame: the WoW character frame backdrop + health/mana bars + name/level text */
static void UI_WriteTargetingFrame(LPEDICT ent) {
    LPPLAYER ps = &ent->client->ps;
    char name_buf[64], level_buf[32], health_buf[32], power_buf[32], xp_buf[32];

    /* Character frame backdrop — drawn with a slight tint matching the original */
    UI_WriteImageUV("Interface\\TargetingFrame\\UI-TargetingFrame.blp",
                    PX(-19), PY(4), PW(232), PH(100),
                    1.0f, 0.09375f, 0.0f, 0.78125f,
                    MAKE(COLOR32, 96, 92, 84, 230));

    /* Dark name area */
    UI_WriteColorRect(PX(87), PY(22), PW(119), PH(41), MAKE(COLOR32, 0, 0, 0, 128));

    /* Name */
    snprintf(name_buf, sizeof(name_buf), "%s",
             ps->name && *ps->name ? ps->name : "Player");
    UI_WriteTextFrame(PX(72), PY(18), PW(100), PH(12),
                      name_buf, MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

    /* Level */
    snprintf(level_buf, sizeof(level_buf), "Lvl %d", (int)ps->stats[WOW_STAT_LEVEL]);
    UI_WriteTextFrame(PX(24), PY(58), PW(42), PH(12),
                      level_buf, MAKE(COLOR32, 235, 225, 190, 255), FONT_JUSTIFYCENTER);

    /* Health bar */
    UI_WriteColorBar(PX(105), PY(38), PW(119), PH(13),
                     (FLOAT)ps->stats[WOW_STAT_HEALTH], (FLOAT)ps->stats[WOW_STAT_HEALTH_MAX],
                     MAKE(COLOR32, 20, 178, 48, 235));
    snprintf(health_buf, sizeof(health_buf), "%u / %u",
             (unsigned)ps->stats[WOW_STAT_HEALTH], (unsigned)ps->stats[WOW_STAT_HEALTH_MAX]);
    UI_WriteTextFrame(PX(105), PY(39), PW(119), PH(11), health_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);

    /* Warrior resource bar: combat generates rage in the server-owned power pool. */
    UI_WriteColorBar(PX(105), PY(52), PW(119), PH(12),
                     (FLOAT)ps->stats[WOW_STAT_POWER], (FLOAT)ps->stats[WOW_STAT_POWER_MAX],
                     MAKE(COLOR32, 180, 35, 28, 235));
    snprintf(power_buf, sizeof(power_buf), "Rage %u / %u",
             (unsigned)ps->stats[WOW_STAT_POWER], (unsigned)ps->stats[WOW_STAT_POWER_MAX]);
    UI_WriteTextFrame(PX(105), PY(53), PW(119), PH(10), power_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);

    /* XP remains visible in the same profile template and refills on level-up. */
    UI_WriteColorBar(PX(105), PY(65), PW(119), PH(10),
                     (FLOAT)ps->stats[WOW_STAT_XP], (FLOAT)ps->stats[WOW_STAT_XP_MAX],
                     MAKE(COLOR32, 150, 80, 210, 235));
    snprintf(xp_buf, sizeof(xp_buf), "XP %u / %u",
             (unsigned)ps->stats[WOW_STAT_XP], (unsigned)ps->stats[WOW_STAT_XP_MAX]);
    UI_WriteTextFrame(PX(105), PY(65), PW(119), PH(10), xp_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);
}

static void UI_WriteStart(DWORD layer) {
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){layer});
    ui_next_frame_number = 1;
}

static void UI_WriteEnd(LPEDICT ent) {
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

/* The original 1.12 client composes the 384x512 quest log from four archived textures. */
void UI_WriteWowQuestLog(LPEDICT ent) {
    static wowHudButton_t const close_button = {
        .path = "Interface\\Buttons\\UI-Panel-MinimizeButton-Up.blp",
        .tooltip = "Close Quest Log",
        .onclick = "questlog close",
        .rect = { PX(323), PY(112), PW(32), PH(32) },
    };

    wowClient_t *client;

    if (!ent || !ent->client) return;
    client = (wowClient_t *)ent->client;
    UI_WriteStart(LAYER_QUESTDIALOG);
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp",
                  PX(4), PY(108), PW(64), PH(64), COLOR32_WHITE);
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-TopLeft.blp",
                  PX(0), PY(104), PW(256), PH(256), COLOR32_WHITE);
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-TopRight.blp",
                  PX(256), PY(104), PW(128), PH(256), COLOR32_WHITE);
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BotLeft.blp",
                  PX(0), PY(360), PW(256), PH(256), COLOR32_WHITE);
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BotRight.blp",
                  PX(256), PY(360), PW(128), PH(256), COLOR32_WHITE);
    UI_WriteTextFrame(PX(42), PY(119), PW(300), PH(16), "QUEST LOG",
                      MAKE(COLOR32, 255, 225, 170, 255), FONT_JUSTIFYCENTER);
    if (client->quest_hud.title[0]) {
        UI_WriteTextFrame(PX(45), PY(174), PW(285), PH(20), client->quest_hud.title,
                          MAKE(COLOR32, 255, 225, 170, 255), FONT_JUSTIFYCENTER);
        UI_WriteTextFrame(PX(45), PY(204), PW(285), PH(52), client->quest_hud.description,
                          MAKE(COLOR32, 225, 210, 175, 255), FONT_JUSTIFYLEFT);
        UI_WriteTextFrame(PX(45), PY(270), PW(285), PH(18), client->quest_hud.progress,
                          COLOR32_WHITE, FONT_JUSTIFYLEFT);
        UI_WriteTextFrame(PX(45), PY(294), PW(285), PH(18), client->quest_hud.status,
                          MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    } else {
        UI_WriteTextFrame(PX(70), PY(174), PW(250), PH(20), "No active quests",
                          MAKE(COLOR32, 255, 225, 170, 255), FONT_JUSTIFYCENTER);
        UI_WriteTextFrame(PX(45), PY(205), PW(285), PH(48),
                          "New quests will appear here when quest gameplay is available.",
                          MAKE(COLOR32, 225, 210, 175, 255), FONT_JUSTIFYCENTER);
    }
    UI_WriteImageButton(&close_button);
    UI_WriteEnd(ent);
}

void UI_HideWowQuestLog(LPEDICT ent) {
    if (!ent || !ent->client) return;
    UI_WriteStart(LAYER_QUESTDIALOG);
    UI_WriteEnd(ent);
}

/* Build and unicast the WoW HUD layer for a player */
void UI_WriteWowHud(LPEDICT ent) {
    static wowHudButton_t const quest_button = {
        .path = "Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp",
        .tooltip = "Open Quest Log",
        .onclick = "questlog toggle",
        .rect = { PX(879), PY(170), PW(40), PH(40) },
    };
    LPPLAYER ps;
    wowClient_t *wc;
    char copper_buf[64];

    if (!ent || !ent->client)
        return;
    ps = &ent->client->ps;
    wc = (wowClient_t *)ent->client;

    UI_WriteStart(LAYER_CONSOLE);

    /* Character/targeting frame (portrait area top-left) */
    UI_WriteTargetingFrame(ent);
    UI_WriteQuestTracker(&wc->quest_hud);

    /* Main action bar + end-caps */
    UI_WriteActionBar();

    /* 12 action buttons, left row */
    FOR_LOOP(i, 12) {
        DWORD img = wc->actions[i].icon[0] ? gi.ImageIndex(wc->actions[i].icon) : 0;
        WOWACTIONDRAW draw = {
            PX(8.0f + (FLOAT)i * 42.0f), PY(728), img, i,
            wc->actions[i].icon[0] ? &wc->actions[i] : NULL
        };

        UI_WriteActionButtonSlot(&draw);
    }

    /* 4 empty button slots, right side */
    FOR_LOOP(i, 4) {
        WOWACTIONDRAW draw = { PX(939.0f - (FLOAT)i * 42.0f), PY(728), 0, 12 + i, NULL };

        UI_WriteActionButtonSlot(&draw);
    }

    UI_WriteInventory(wc);
    UI_WriteLootPrompt(wc->loot_target);

    if (wc->combat_message.time && wc->combat_message.text[0]) {
        UI_WriteColorRect(PX(362), PY(86), PW(300), PH(24), MAKE(COLOR32, 5, 8, 12, 190));
        UI_WriteTextFrame(PX(370), PY(91), PW(284), PH(16), wc->combat_message.text,
                          MAKE(COLOR32, 255, 220, 135, 255), FONT_JUSTIFYCENTER);
    }

    /* Backpack */
    UI_WriteImage("Interface\\Buttons\\Button-Backpack-Up.blp",
                  PX(981), PY(729), PW(37), PH(37), COLOR32_WHITE);

    /* Clean minimap + live zone header */
    UI_WriteMinimapFrames(wc->zone_name);

    /* Quest control sits below the fixed top-right minimap and opens its own layer. */
    UI_WriteImageButton(&quest_button);
    UI_WriteTextFrame(PX(919), PY(180), PW(88), PH(20),
                      "Quest Log", MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);

    /* Copper display */
    snprintf(copper_buf, sizeof(copper_buf), "Copper %d", (int)ps->stats[WOW_STAT_COPPER]);
    UI_WriteTextFrame(PX(816), PY(704), PW(150), PH(20),
                      copper_buf, MAKE(COLOR32, 255, 210, 100, 255), FONT_JUSTIFYRIGHT);

    UI_WriteEnd(ent);
}
