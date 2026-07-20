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

- Idle creatures acquire the player within `BZ_WOW_CREATURE_AGGRO_RANGE`.
- Player melee and projectile damage use `Wow_DealDamage` and assign the projectile caster as the aggro owner.
- Chase stops at `WOW_MELEE_RANGE`; damage occurs once at the animation damage point, followed by the existing
  backswing.
- A player or creature beyond `BZ_WOW_CREATURE_LEASH_RANGE` from the spawn point triggers Evade.
- Evade clears the enemy immediately, returns to `home`, and regenerates one health per
  `BZ_WOW_CREATURE_REGEN_TIME`.
- Death clears selections, attackers, and in-flight projectiles that reference the victim. The creature keeps its edict
  but temporarily loses `SVF_MONSTER`, so it is neither a living target nor a dynamic obstacle.
- Respawn restores the same edict at `home`, resets all combat timers and references, restores full health, and raises
  `SVF_MONSTER` again.

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
