#include "g_wow_local.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static WOWQUESTDEF const wow_quest_defs[WOW_QUEST_COUNT] = {
    [WOW_QUEST_FIRST_HUNT] = {
        WOW_QUEST_FIRST_HUNT,
        "first_hunt",
        "The First Hunt",
        "Defeat four hostile creatures and return to the quest giver.",
        WOW_QUEST_OBJECTIVE_KILL,
        4,
        0,
        250,
        { WOW_ITEM_MINOR_HEALING_POTION, 1 },
        WOW_QUEST_NONE,
    },
};

LPCWOWQUESTDEF Wow_QuestDef(wowQuestId_t quest) {
    return quest > WOW_QUEST_NONE && quest < WOW_QUEST_COUNT ? &wow_quest_defs[quest] : NULL;
}

wowQuestId_t Wow_QuestId(LPCSTR key) {
    if (!key || !*key) return WOW_QUEST_NONE;
    for (wowQuestId_t quest = WOW_QUEST_FIRST_HUNT; quest < WOW_QUEST_COUNT; quest++)
        if (!strcasecmp(key, wow_quest_defs[quest].key)) return quest;
    return WOW_QUEST_NONE;
}

static wowClient_t *Wow_QuestClient(LPEDICT player) {
    wowEntityLocal_t *local = Wow_EntityLocal(player);

    return player && player->client && local && local->kind == WOW_ENTITY_PLAYER
        ? (wowClient_t *)player->client : NULL;
}

static BOOL Wow_IsQuestGiver(LPEDICT player, LPEDICT giver, wowQuestId_t quest) {
    wowEntityLocal_t *local = Wow_EntityLocal(giver);

    return player && giver && local && giver->inuse && !local->dead &&
        local->kind == WOW_ENTITY_CREATURE && local->quest_giver == quest &&
        Wow_Distance2(&player->s.origin2, &giver->s.origin2) <= BZ_WOW_QUEST_INTERACTION_RANGE;
}

void Wow_InitQuestProgress(LPEDICT player) {
    wowClient_t *client = Wow_QuestClient(player);

    if (!client) return;
    memset(&client->quest, 0, sizeof(client->quest));
    memset(&client->quest_hud, 0, sizeof(client->quest_hud));
    client->quest.id = WOW_QUEST_FIRST_HUNT;
    client->quest.state = WOW_QUEST_AVAILABLE;
    client->ui_flags |= BZ_WOW_UI_DIRTY;
}

/* One controlled ambient creature becomes neutral while keeping its normal selectable world entity. */
void Wow_SetQuestGiver(LPEDICT creature, wowQuestId_t quest) {
    wowEntityLocal_t *local = Wow_EntityLocal(creature);

    if (!creature || !local || local->kind != WOW_ENTITY_CREATURE || !Wow_QuestDef(quest)) return;
    local->quest_giver = quest;
    local->hostile = false;
    local->enemy = NULL;
    local->ai_state = WOW_AI_IDLE;
    local->patrol_radius = local->walk_speed = 0.0f;
    creature->s.renderfx &= ~RF_HOSTILE;
    Wow_SetStandMove(creature);
}

BOOL Wow_AcceptQuest(LPEDICT player, LPEDICT giver, wowQuestId_t quest) {
    wowClient_t *client = Wow_QuestClient(player);

    if (!client || client->quest.id != quest || client->quest.state != WOW_QUEST_AVAILABLE ||
        !Wow_IsQuestGiver(player, giver, quest)) {
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_NOT_READY, 0);
        return false;
    }
    client->quest.state = WOW_QUEST_ACTIVE;
    client->quest.count = 0;
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_ACCEPTED, 0);
    return true;
}

/* Confirmed combat ownership reaches this function only from the guarded Wow_AIDie transition. */
void Wow_QuestCreatureKilled(LPEDICT player, LPEDICT victim) {
    wowClient_t *client = Wow_QuestClient(player);
    wowEntityLocal_t *victim_local = Wow_EntityLocal(victim);
    LPCWOWQUESTDEF def;

    if (!client || !victim || !victim_local || client->quest.state != WOW_QUEST_ACTIVE) return;
    def = Wow_QuestDef(client->quest.id);
    if (!def || def->objective != WOW_QUEST_OBJECTIVE_KILL ||
        victim_local->kind != WOW_ENTITY_CREATURE || !victim_local->hostile ||
        (def->target_display_id && def->target_display_id != victim_local->display_id)) return;
    client->quest.count = MIN(client->quest.count + 1, def->required_count);
    if (client->quest.count >= def->required_count) {
        client->quest.state = WOW_QUEST_READY_TO_TURN_IN;
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_READY, client->quest.count);
    } else {
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_PROGRESS, client->quest.count);
    }
    client->ui_flags |= BZ_WOW_UI_DIRTY;
}

/* Item insertion is already atomic; only after it succeeds can infallible XP and completion be committed. */
BOOL Wow_TurnInQuest(LPEDICT player, LPEDICT giver, wowQuestId_t quest) {
    wowClient_t *client = Wow_QuestClient(player);
    LPCWOWQUESTDEF def = Wow_QuestDef(quest);

    if (!client || !def || client->quest.id != quest ||
        client->quest.state != WOW_QUEST_READY_TO_TURN_IN ||
        !Wow_IsQuestGiver(player, giver, quest)) {
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_NOT_READY, 0);
        return false;
    }
    if (def->item_reward.item != WOW_ITEM_NONE &&
        !Wow_GiveItem(player, def->item_reward.item, def->item_reward.count)) {
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_INVENTORY_FULL, 0);
        return false;
    }
    client->quest.state = WOW_QUEST_COMPLETED;
    client->quest.count = def->required_count;
    Wow_AwardXp(player, def->xp_reward);
    Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_QUEST_COMPLETED, 0);
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    return true;
}

static LPEDICT Wow_SelectedQuestGiver(LPEDICT player, wowQuestId_t quest) {
    DWORD number;

    if (!player || !player->client) return NULL;
    number = player->client->ps.selected_entity;
    if (!number || number >= (DWORD)globals.num_edicts || number >= WOW_MAX_EDICTS) return NULL;
    return Wow_IsQuestGiver(player, &wow_edicts[number], quest) ? &wow_edicts[number] : NULL;
}

/* The HUD snapshot is derived from the definition and progress; commands never carry mutable quest truth. */
BOOL Wow_UpdateQuestUiState(LPEDICT player) {
    wowClient_t *client = Wow_QuestClient(player);
    WOWQUESTHUD hud = { 0 };
    LPCWOWQUESTDEF def;
    LPEDICT giver;

    if (!client) return false;
    def = Wow_QuestDef(client->quest.id);
    if (!def) {
        BOOL changed = memcmp(&client->quest_hud, &hud, sizeof(hud)) != 0;
        client->quest_hud = hud;
        return changed;
    }
    snprintf(hud.title, sizeof(hud.title), "%s", def->title);
    snprintf(hud.description, sizeof(hud.description), "%s", def->description);
    snprintf(hud.progress, sizeof(hud.progress), "%u / %u hostile creatures",
             (unsigned)client->quest.count, (unsigned)def->required_count);
    giver = Wow_SelectedQuestGiver(player, def->id);
    switch (client->quest.state) {
        case WOW_QUEST_AVAILABLE:
            snprintf(hud.status, sizeof(hud.status), "%s", "Available");
            if (giver) {
                snprintf(hud.action_label, sizeof(hud.action_label), "%s", "Accept Quest");
                snprintf(hud.action_command, sizeof(hud.action_command), "quest_accept %s", def->key);
            }
            break;
        case WOW_QUEST_ACTIVE:
            snprintf(hud.status, sizeof(hud.status), "%s", "In progress");
            hud.tracker_visible = true;
            break;
        case WOW_QUEST_READY_TO_TURN_IN:
            snprintf(hud.status, sizeof(hud.status), "%s", "Ready to turn in");
            hud.tracker_visible = true;
            if (giver) {
                snprintf(hud.action_label, sizeof(hud.action_label), "%s", "Turn In");
                snprintf(hud.action_command, sizeof(hud.action_command), "quest_turn_in %s", def->key);
            }
            break;
        case WOW_QUEST_COMPLETED:
            snprintf(hud.status, sizeof(hud.status), "%s", "Completed");
            break;
        default:
            snprintf(hud.status, sizeof(hud.status), "%s", "Unavailable");
            break;
    }
    if (!memcmp(&client->quest_hud, &hud, sizeof(hud))) return false;
    client->quest_hud = hud;
    return true;
}
