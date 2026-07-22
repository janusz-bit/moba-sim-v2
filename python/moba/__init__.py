"""moba-sim: MOBA-style champion stat aggregation and combat simulation.

High-level API:
    from moba import Champion, Simulation, Stat, ModType, TypeDamage, Source

    champ = Champion({Stat.MaxHP: 1000, Stat.AD: 50, Stat.AR: 100})
    final = champ.evaluate_champion()

    sim = Simulation()
    sim.champions.append(attacker)
    sim.champions.append(target)
    sim.emit_attack_hit(0, 1, 100.0, TypeDamage.Physical)

See docs/architecture.md for the full system design.
"""

from .moba_ext import (
    # Enums
    Stat,
    ModType,
    TypeDamage,
    # Source
    Source,
    # Events
    AttackHit,
    DamageDealt,
    DamageReceived,
    HealApplied,
    Death,
    # Core
    Champion,
    ModDB,
    Simulation,
    ConvergenceError,
    # Combat helpers
    mitigated_damage,
    post_mitigation_damage,
    apply_damage_to_shield,
    get_stat,
    set_stat,
)

__all__ = [
    "Stat",
    "ModType",
    "TypeDamage",
    "Source",
    "AttackHit",
    "DamageDealt",
    "DamageReceived",
    "HealApplied",
    "Death",
    "Champion",
    "ModDB",
    "Simulation",
    "ConvergenceError",
    "mitigated_damage",
    "post_mitigation_damage",
    "apply_damage_to_shield",
    "get_stat",
    "set_stat",
]

__version__ = "0.1.0"