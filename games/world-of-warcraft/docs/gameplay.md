# OpenWoW Gameplay Runtime

## Creature Combat Lifecycle

Creature combat is server-owned and advances once per `FRAMETIME` through the existing chain:

```text
Wow_RunFrame
  -> Wow_RunCreatureFrame
  -> Wow_AIRunFrame
  -> Wow_DealDamage
  -> health / rage / XP
  -> entityState_t and playerState_t
```

`wowAiState_t` is the creature behavior state machine:

```text
Idle -> Aggro -> Chase -> Attack
                   |        |
                   +-> Evade
Attack -> Dead -> Respawn -> Idle
```

- Idle creatures acquire the player within `BZ_WOW_CREATURE_AGGRO_RANGE` only
  when the central world sweep reports clear sight.
- Player melee and projectile damage use `Wow_DealDamage` and assign the projectile caster as the aggro owner.
- Chase uses deterministic local obstacle probes and a retained steering direction; repeated failure recovers toward
  a distinct last-valid checkpoint before Evade.
- Chase stops at `WOW_MELEE_RANGE` only with clear sight. Melee rechecks range and sight at the animation damage point,
  then enters the existing backswing.
- Homing projectiles sweep their complete frame segment against WMO collision. A wall hit removes the projectile
  before damage, kill credit, XP, loot, or quest progress.
- A player or creature beyond `BZ_WOW_CREATURE_LEASH_RANGE` from the spawn point triggers Evade.
- Evade clears the enemy immediately, returns to `home`, and regenerates one health per
  `BZ_WOW_CREATURE_REGEN_TIME`.
- Death clears selections, attackers, and in-flight projectiles that reference the victim. The creature keeps its edict
  but temporarily loses `SVF_MONSTER`, so it is neither a living target nor a dynamic obstacle.
- Respawn restores the same edict at `home`, resets all combat timers and references, restores full health, and raises
  `SVF_MONSTER` again. It also clears all transient obstacle navigation state.

Tests use artificial entities and animations in `tests/test_wow_combat.c`; no files from `data/` are fixtures.

## Player Abilities

The number-key path remains server authoritative:

```text
CL_HandleGameKey
  -> wow_action
  -> Wow_UseAbility
  -> Wow_AIAttack or Wow_FireFirebolt
  -> Wow_DealDamage
  -> rage / death / projectile kill credit / XP
  -> UI_WriteWowHud
```

The initial action bar contains:

- `1` Strike: one base-damage application at the existing melee animation damage point.
- `2` Heavy Strike: 20 Rage, three damage, and a 1.5-second server cooldown.
- `3` Throw: the existing homing projectile and caster-based kill-credit path with a two-second server cooldown.

`wowEntityLocal_t.ability_cooldown` owns execution timing. The first three `wowHudIcon_t` entries contain quantized
cooldown, Rage cost, and disabled flags derived from the same target, range, life, resource, and attack state used by
`Wow_UseAbility`. The HUD renders those fields but never decides whether an ability succeeds.

Combat feedback is a single replace-in-place `WOWCOMBATMESSAGE` per client. Damage, XP, level-up, range, Rage, and
cooldown events replace the previous message and expire after `BZ_WOW_COMBAT_MESSAGE_TIME`; there is no growing
floating-text queue.

Ability tests use artificial entities, animations, and a one-byte projectile-model fixture in
`tests/test_wow_abilities.c`.

## Loot, Inventory, and Equipment

Creature loot continues the same server-owned death path:

```text
Wow_AIDie
  -> Wow_GenerateLoot
  -> corpse-owned fixed loot list
  -> loot <entity>
  -> atomic Wow_LootCreature preflight
  -> wowClient_t.bag
  -> use_item <slot>
  -> health or equipment
  -> UI_WriteWowHud
```

The starter table contains a stackable Minor Healing Potion, a Training Sword, and Padded Armor. The six-slot bag
stacks items up to their item-table limit. Loot pickup first applies the complete corpse list to a copied bag; if any
item does not fit, neither bag nor corpse changes. A successful pickup marks that corpse as picked, and respawn clears
all remaining corpse loot.

Potions heal the authoritative player health up to its maximum and consume one stack entry. Equipment stores one item
ID for each weapon and armor slot. The weapon bonus enters the existing ability damage point, while armor reduction
enters `Wow_DealDamage`, the central incoming-damage path. The HUD exposes only derived icons, counts, equipment text,
and `loot`/`use_item` commands; it never owns item stats.

Inventory tests use only fixed artificial entities and item IDs in `tests/test_wow_abilities.c`,
`tests/test_wow_combat.c`, `tests/test_wow_game.c`, and `tests/test_wow_hud.c`.

## First Kill Quest

`g_quest.c` keeps immutable `WOWQUESTDEF` data separate from the player's `WOWQUESTPROGRESS`. The first definition is
`first_hunt`: defeat four hostile creatures, then receive 250 XP and one Minor Healing Potion. Progress follows one
exclusive state machine:

```text
Available -> Active -> ReadyToTurnIn -> Completed
```

The controlled fifth ambient creature is a neutral, selectable quest giver. Selecting it within
`BZ_WOW_QUEST_INTERACTION_RANGE` derives an `Accept Quest` or `Turn In` HUD command from the current state. Both the
HUD and the optional `quest_accept first_hunt`, `quest_turn_in first_hunt`, and `quest_status` development commands
call the same server functions.

Kill progress extends the existing confirmed-death chain:

```text
Wow_DealDamage
  -> Wow_AIDie
  -> Wow_CombatOwner
  -> existing XP and loot
  -> Wow_QuestCreatureKilled
  -> derived quest HUD
```

The guarded `Wow_AIDie` transition guarantees one count per living spawn. Projectile ownership resolves to its
caster, while neutral creatures, creature-owned deaths, evade, and duplicate death calls do not advance the quest.
At four kills the counter clamps and becomes ready for turn-in.

Turn-in first inserts the complete item reward through the existing copied-bag `Wow_GiveItem` path. Only a successful
insert commits completion and calls the central `Wow_AwardXp`; therefore a full bag leaves the quest ready, grants no
XP, and cannot create a partial or duplicate reward. Quest tests use synthetic entities and fixed item/quest IDs only.

## Persistent Player Progress

`g_progress.c` snapshots only the authoritative local player's RPG state: level, XP, current/max health, Rage, the
six bag stacks, weapon and armor equipment, and the first quest's state and count. Edict pointers, aggro, cooldowns,
corpse loot, NPC health/AI, renderer handles, and asset data never enter the save.

The committed OpenWoW config selects `share/openwow-progress.sav` through the archived `wow_save` cvar. This uses the
same writable local `share/` area as the existing user config and stays outside `data/`; the save and its temporary
file are ignored by Git. The path remains relative and can be overridden for diagnostics.

The text schema starts with `OPENWOW_PROGRESS 1`. Loading parses into a detached snapshot, rejects malformed or future
versions, validates stable item/quest IDs and structural limits, and clamps current health and Rage before applying
the snapshot. Equipment must refer to a correctly typed item present in the loaded bag. A missing file keeps the
normal new-player defaults. A rejected file disables automatic saving for that session so shutdown cannot overwrite
corrupt evidence or data from a future version.

Saving writes `<wow_save>.tmp`, flushes and synchronizes it, then atomically renames it over the destination. Progress
loads during player initialization, saves on regular shutdown and before a map transition, and can be controlled from
the runtime console:

```text
cmd save_progress
cmd reset_progress confirm
```

The confirmed reset writes the same default snapshot used for a new player before applying it to runtime. Save/load
tests use small text fixtures under `build/tests/`; they never read the real WoW archives or `data/`.
