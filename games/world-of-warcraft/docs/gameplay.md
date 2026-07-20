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
