/*
 * ui_gm.c — Native OpenWoW exploration controls.
 *
 * The original WoW UI archives do not contain OpenWoW's server commands, so
 * this small HUD layer keeps GM travel reachable after the glue UI shuts down.
 */
#include "ui_local.h"

#define BZ_WOW_GM_MOUSE_LEFT 1
#define BZ_WOW_GM_TILE_STEP  533.333313f

typedef enum {
    WOW_GM_ENABLE,
    WOW_GM_DISABLE,
    WOW_GM_X_NEG,
    WOW_GM_X_POS,
    WOW_GM_Y_NEG,
    WOW_GM_Y_POS,
} WOWGMACTION;

typedef struct {
    RECT rect;
    LPCSTR label;
    WOWGMACTION action;
} WOWGMBUTTON;
typedef WOWGMBUTTON *LPWOWGMBUTTON;
typedef WOWGMBUTTON const *LPCWOWGMBUTTON;

static RECT const wow_gm_toggle = { 0.775f, 0.02f, 0.07f, 0.05f };
static RECT const wow_gm_panel = { 0.56f, 0.08f, 0.285f, 0.34f };
static RECT const wow_gm_title = { 0.575f, 0.09f, 0.255f, 0.035f };
static RECT const wow_gm_position = { 0.575f, 0.375f, 0.255f, 0.03f };
static WOWGMBUTTON const wow_gm_buttons[] = {
    { { 0.575f, 0.14f,  0.12f, 0.055f }, "GM ON (10x)", WOW_GM_ENABLE },
    { { 0.71f,  0.14f,  0.12f, 0.055f }, "GM OFF",      WOW_GM_DISABLE },
    { { 0.575f, 0.215f, 0.12f, 0.055f }, "X - 1 tile",  WOW_GM_X_NEG },
    { { 0.71f,  0.215f, 0.12f, 0.055f }, "X + 1 tile",  WOW_GM_X_POS },
    { { 0.575f, 0.29f,  0.12f, 0.055f }, "Y - 1 tile",  WOW_GM_Y_NEG },
    { { 0.71f,  0.29f,  0.12f, 0.055f }, "Y + 1 tile",  WOW_GM_Y_POS },
};

static struct {
    BOOL open;
    int pressed;
} wow_gm;

/* Keep the overlay state independent of whichever archived UI Lua is active. */
void UIWow_GmInit(void) {
    wow_gm.open = false;
    wow_gm.pressed = -1;
}

static void UIWow_GmDrawText(LPCFONT font, LPCSTR text, LPCRECT rect) {
    if (!font) return;
    wow_ui.renderer->DrawText(&MAKE(drawText_t,
        .font = font, .text = text, .rect = *rect, .color = COLOR32_WHITE,
        .textWidth = rect->w, .lineHeight = rect->h,
        .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
}

/* Draw after the archived HUD so OpenWoW-only controls remain visible and clickable. */
void UIWow_GmDraw(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPTEXTURE background;
    LPCFONT font;
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    char position[96];

    if (!ps || ps->client_ui_state != CLIENT_UI_GAME || !wow_ui.renderer) return;
    background = UIWow_LoadTexture("Interface\\Tooltips\\UI-Tooltip-Background.blp");
    font = UIWow_LoadFont(14);
    wow_ui.renderer->DrawImage(background, &wow_gm_toggle, &uv, wow_gm.open
        ? MAKE(COLOR32, 220, 165, 65, 245) : MAKE(COLOR32, 120, 145, 180, 235));
    UIWow_GmDrawText(font, wow_gm.open ? "GM X" : "GM", &wow_gm_toggle);
    if (!wow_gm.open) return;

    wow_ui.renderer->DrawImage(background, &wow_gm_panel, &uv, MAKE(COLOR32, 90, 105, 130, 245));
    UIWow_GmDrawText(font, "EXPLORATION", &wow_gm_title);
    FOR_LOOP(i, sizeof(wow_gm_buttons) / sizeof(wow_gm_buttons[0])) {
        LPCWOWGMBUTTON button = &wow_gm_buttons[i];
        wow_ui.renderer->DrawImage(background, &button->rect, &uv, (int)i == wow_gm.pressed
            ? MAKE(COLOR32, 220, 165, 65, 250) : MAKE(COLOR32, 130, 155, 190, 240));
        UIWow_GmDrawText(font, button->label, &button->rect);
    }
    snprintf(position, sizeof(position), "Position  X %.1f   Y %.1f", (double)ps->origin.x, (double)ps->origin.y);
    UIWow_GmDrawText(font, position, &wow_gm_position);
}

static void UIWow_GmRunAction(WOWGMACTION action, LPCPLAYER ps) {
    char command[96];
    FLOAT x = ps->origin.x, y = ps->origin.y;

    if (action == WOW_GM_ENABLE) { uiimport.ServerCommand("gm on"); return; }
    if (action == WOW_GM_DISABLE) { uiimport.ServerCommand("gm off"); return; }
    if (action == WOW_GM_X_NEG) x -= BZ_WOW_GM_TILE_STEP;
    else if (action == WOW_GM_X_POS) x += BZ_WOW_GM_TILE_STEP;
    else if (action == WOW_GM_Y_NEG) y -= BZ_WOW_GM_TILE_STEP;
    else if (action == WOW_GM_Y_POS) y += BZ_WOW_GM_TILE_STEP;
    snprintf(command, sizeof(command), "teleport %.3f %.3f", (double)x, (double)y);
    uiimport.ServerCommand(command);
}

/* Consume clicks over the native panel before selection/movement sees them. */
BOOL UIWow_GmMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    VECTOR2 point = UIWow_MouseFdf(x, y);
    BOOL over_toggle = Rect_contains(&wow_gm_toggle, &point);
    BOOL over_panel = wow_gm.open && Rect_contains(&wow_gm_panel, &point);
    int hit = -1;

    if (!ps || ps->client_ui_state != CLIENT_UI_GAME) return false;
    if (wow_gm.open)
        FOR_LOOP(i, sizeof(wow_gm_buttons) / sizeof(wow_gm_buttons[0]))
            if (Rect_contains(&wow_gm_buttons[i].rect, &point)) { hit = (int)i; break; }
    if (event == UI_MOUSE_DOWN && param == BZ_WOW_GM_MOUSE_LEFT) {
        wow_gm.pressed = over_toggle ? (int)(sizeof(wow_gm_buttons) / sizeof(wow_gm_buttons[0])) : hit;
        return over_toggle || over_panel;
    }
    if (event == UI_MOUSE_UP && param == BZ_WOW_GM_MOUSE_LEFT) {
        int pressed = wow_gm.pressed;
        wow_gm.pressed = -1;
        if (over_toggle && pressed == (int)(sizeof(wow_gm_buttons) / sizeof(wow_gm_buttons[0]))) {
            wow_gm.open = !wow_gm.open;
            return true;
        }
        if (hit >= 0 && pressed == hit) {
            UIWow_GmRunAction(wow_gm_buttons[hit].action, ps);
            return true;
        }
        return over_toggle || over_panel || pressed >= 0;
    }
    return over_toggle || over_panel;
}
