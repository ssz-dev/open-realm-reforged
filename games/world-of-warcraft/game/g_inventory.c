#include "g_wow_local.h"
#include <stdio.h>
#include <string.h>

static WOWITEMDEF const wow_item_defs[WOW_ITEM_COUNT] = {
    [WOW_ITEM_MINOR_HEALING_POTION] = {
        WOW_ITEM_MINOR_HEALING_POTION, WOW_ITEM_CONSUMABLE, "Minor Healing Potion",
        "Interface\\Icons\\INV_Potion_51.blp", 5, 25
    },
    [WOW_ITEM_TRAINING_SWORD] = {
        WOW_ITEM_TRAINING_SWORD, WOW_ITEM_WEAPON, "Training Sword",
        "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", 1, 1
    },
    [WOW_ITEM_PADDED_ARMOR] = {
        WOW_ITEM_PADDED_ARMOR, WOW_ITEM_ARMOR, "Padded Armor",
        "Interface\\Icons\\INV_Misc_Bag_08.blp", 1, 1
    },
};

LPCWOWITEMDEF Wow_ItemDef(wowItemId_t item) {
    return item > WOW_ITEM_NONE && item < WOW_ITEM_COUNT ? &wow_item_defs[item] : NULL;
}

/* All inventory mutation uses a copied fixed bag so multi-item loot either fits completely or changes nothing. */
static BOOL Wow_AddItemStack(LPWOWITEMSTACK bag, wowItemId_t item, DWORD count) {
    LPCWOWITEMDEF def = Wow_ItemDef(item);

    if (!bag || !def || !count) return false;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        DWORD room;

        if (bag[i].item != item || bag[i].count >= def->max_stack) continue;
        room = def->max_stack - bag[i].count;
        bag[i].count += MIN(room, count);
        count -= MIN(room, count);
        if (!count) return true;
    }
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        if (bag[i].item != WOW_ITEM_NONE) continue;
        bag[i].item = item;
        bag[i].count = MIN(def->max_stack, count);
        count -= bag[i].count;
        if (!count) return true;
    }
    return false;
}

static wowClient_t *Wow_InventoryClient(LPEDICT player) {
    wowEntityLocal_t *local = Wow_EntityLocal(player);

    return player && player->client && local && local->kind == WOW_ENTITY_PLAYER
        ? (wowClient_t *)player->client : NULL;
}

BOOL Wow_GiveItem(LPEDICT player, wowItemId_t item, DWORD count) {
    wowClient_t *client = Wow_InventoryClient(player);
    WOWITEMSTACK bag[WOW_UI_INVENTORY_SLOTS];

    if (!client) return false;
    memcpy(bag, client->bag, sizeof(bag));
    if (!Wow_AddItemStack(bag, item, count)) return false;
    memcpy(client->bag, bag, sizeof(bag));
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    return true;
}

/* Every creature corpse owns its deterministic starter loot until pickup or respawn. */
void Wow_GenerateLoot(LPEDICT creature) {
    static WOWITEMSTACK const starter_loot[BZ_WOW_MAX_LOOT_ITEMS] = {
        { WOW_ITEM_MINOR_HEALING_POTION, 1 },
        { WOW_ITEM_TRAINING_SWORD, 1 },
        { WOW_ITEM_PADDED_ARMOR, 1 },
    };
    wowEntityLocal_t *local = Wow_EntityLocal(creature);

    if (!creature || !local || local->kind != WOW_ENTITY_CREATURE || local->loot_state != WOW_LOOT_NONE) return;
    memcpy(local->loot, starter_loot, sizeof(starter_loot));
    local->num_loot = BZ_WOW_MAX_LOOT_ITEMS;
    local->loot_state = WOW_LOOT_AVAILABLE;
}

void Wow_ClearLoot(LPEDICT creature) {
    wowEntityLocal_t *local = Wow_EntityLocal(creature);

    if (!local) return;
    memset(local->loot, 0, sizeof(local->loot));
    local->num_loot = 0;
    local->loot_state = WOW_LOOT_NONE;
}

/* Atomic pickup keeps an unavailable corpse unchanged when the complete loot list cannot fit. */
BOOL Wow_LootCreature(LPEDICT player, LPEDICT creature) {
    wowClient_t *client = Wow_InventoryClient(player);
    wowEntityLocal_t *player_local = Wow_EntityLocal(player);
    wowEntityLocal_t *creature_local = Wow_EntityLocal(creature);
    WOWITEMSTACK bag[WOW_UI_INVENTORY_SLOTS];

    if (!client || !player_local || player_local->dead || !creature || !creature_local ||
        creature_local->kind != WOW_ENTITY_CREATURE || !creature_local->dead ||
        creature_local->loot_state != WOW_LOOT_AVAILABLE ||
        Wow_Distance2(&player->s.origin2, &creature->s.origin2) > BZ_WOW_LOOT_RANGE) return false;
    memcpy(bag, client->bag, sizeof(bag));
    FOR_LOOP(i, creature_local->num_loot) {
        if (i >= BZ_WOW_MAX_LOOT_ITEMS ||
            !Wow_AddItemStack(bag, creature_local->loot[i].item, creature_local->loot[i].count)) {
            Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_INVENTORY_FULL, 0);
            return false;
        }
    }
    memcpy(client->bag, bag, sizeof(bag));
    memset(creature_local->loot, 0, sizeof(creature_local->loot));
    creature_local->num_loot = 0;
    creature_local->loot_state = WOW_LOOT_PICKED;
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_LOOT, 0);
    return true;
}

/* Consumables mutate real vitals; equipment selects exactly one item ID in each fixed equipment slot. */
BOOL Wow_UseInventorySlot(LPEDICT player, DWORD slot) {
    wowClient_t *client = Wow_InventoryClient(player);
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    WOWITEMSTACK *stack;
    LPCWOWITEMDEF def;
    DWORD healed;

    if (!client || !local || local->dead || slot >= WOW_UI_INVENTORY_SLOTS) return false;
    stack = &client->bag[slot];
    def = Wow_ItemDef(stack->item);
    if (!def || !stack->count) return false;
    if (def->type == WOW_ITEM_CONSUMABLE) {
        if (local->health >= local->max_health) return false;
        healed = MIN(def->value, local->max_health - local->health);
        local->health += healed;
        if (!--stack->count) memset(stack, 0, sizeof(*stack));
        Wow_SyncEntityVitals(player);
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_HEAL, healed);
    } else {
        wowEquipmentSlot_t equipment_slot = def->type == WOW_ITEM_WEAPON
            ? WOW_EQUIPMENT_WEAPON : WOW_EQUIPMENT_ARMOR;

        client->equipment[equipment_slot] = def->id;
        Wow_SetCombatMessage(player, WOW_COMBAT_MESSAGE_EQUIP, 0);
    }
    client->ui_flags |= BZ_WOW_UI_DIRTY;
    return true;
}

DWORD Wow_PlayerWeaponBonus(LPEDICT player) {
    wowClient_t *client = Wow_InventoryClient(player);
    LPCWOWITEMDEF def = client ? Wow_ItemDef(client->equipment[WOW_EQUIPMENT_WEAPON]) : NULL;

    return def && def->type == WOW_ITEM_WEAPON ? def->value : 0;
}

/* Armor reduction belongs at the single incoming-damage boundary and always leaves at least one damage. */
DWORD Wow_AdjustIncomingDamage(LPEDICT target, DWORD damage) {
    wowClient_t *client = Wow_InventoryClient(target);
    LPCWOWITEMDEF def = client ? Wow_ItemDef(client->equipment[WOW_EQUIPMENT_ARMOR]) : NULL;

    if (!damage || !def || def->type != WOW_ITEM_ARMOR) return damage;
    return MAX(1, damage > def->value ? damage - def->value : 1);
}

static DWORD Wow_NearbyLootTarget(LPEDICT player) {
    DWORD target = 0;
    FLOAT best = BZ_WOW_LOOT_RANGE;

    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts && i < WOW_MAX_EDICTS; i++) {
        LPEDICT creature = &wow_edicts[i];
        wowEntityLocal_t *local = Wow_EntityLocal(creature);
        FLOAT distance;

        if (!creature->inuse || !local || local->kind != WOW_ENTITY_CREATURE || !local->dead ||
            local->loot_state != WOW_LOOT_AVAILABLE) continue;
        distance = Wow_Distance2(&player->s.origin2, &creature->s.origin2);
        if (distance > best) continue;
        best = distance;
        target = creature->s.number;
    }
    return target;
}

/* The native HUD model is derived from item IDs and never carries item stats back into gameplay. */
BOOL Wow_UpdateInventoryUiState(LPEDICT player) {
    wowClient_t *client = Wow_InventoryClient(player);
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS] = { 0 };
    char equipment_text[sizeof(client->equipment_text)];
    LPCWOWITEMDEF weapon, armor;
    DWORD loot_target;
    BOOL changed;

    if (!client) return false;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        LPCWOWITEMDEF def = Wow_ItemDef(client->bag[i].item);

        if (!def || !client->bag[i].count) continue;
        snprintf(inventory[i].icon, sizeof(inventory[i].icon), "%s", def->icon);
        snprintf(inventory[i].name, sizeof(inventory[i].name), "%s", def->name);
        inventory[i].count = client->bag[i].count;
    }
    weapon = Wow_ItemDef(client->equipment[WOW_EQUIPMENT_WEAPON]);
    armor = Wow_ItemDef(client->equipment[WOW_EQUIPMENT_ARMOR]);
    snprintf(equipment_text, sizeof(equipment_text), "Weapon: %s | Armor: %s",
             weapon ? weapon->name : "none", armor ? armor->name : "none");
    loot_target = Wow_NearbyLootTarget(player);
    changed = memcmp(client->inventory, inventory, sizeof(inventory)) ||
        strcmp(client->equipment_text, equipment_text) || client->loot_target != loot_target;
    memcpy(client->inventory, inventory, sizeof(inventory));
    snprintf(client->equipment_text, sizeof(client->equipment_text), "%s", equipment_text);
    client->loot_target = loot_target;
    return changed;
}
