Overview
========

Architecture
------------

The library is organized into focused modules under ``include/moba/``:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Module
     - Contents
   * - :cpp:type:`moba::Stat`
     - 25 champion statistics (MaxHP, AD, AR, LifeSteal, ...)
   * - :cpp:type:`moba::ModDB`
     - Base/Inc/More modifier pipeline: ``getStat = sum * inc * more``
   * - :cpp:type:`moba::Champion`
     - Champion with base stats + passive effects queue
   * - :cpp:type:`moba::Signal`
     - Typed publish-subscribe signals for event dispatch
   * - :cpp:type:`moba::Simulation`
     - Multi-champion combat with built-in damage, lifesteal, death, healing

Modifier Pipeline
-----------------

A stat's final value combines three modifier types:

- **Base** — additive bonuses (``10 + 20 + 30 = 60``)
- **Inc** — multiplicative increases summed from 1.0 (``1.0 + 0.1 + 0.2 = 1.3``)
- **More** — multiplicative multipliers from 1.0 (``1.1 * 1.2 * 1.3 = 1.716``)

Final stat = ``sum(Base) * (1 + sum(Inc)) * product(More)``.

Passives
--------

A :cpp:type:`moba::Champion::Passive` is a ``std::function`` that receives
``(base, final, time)`` and returns modifiers + an ``alive`` flag.
The passive is the sole authority on its lifetime:

- **permanent** — always returns ``alive=true``
- **one-shot** — returns ``alive=false`` after one application
- **temp** — returns ``alive=false`` when it decides to expire

Event System
------------

:cpp:type:`moba::Simulation` owns one :cpp:type:`moba::Signal` per event type.
The constructor wires internal handlers:

1. ``AttackHit`` → mitigated damage → emit ``DamageReceived`` + ``DamageDealt``
2. ``DamageDealt`` → lifesteal (physical) + omnivamp (all) → emit ``HealApplied``
3. ``DamageReceived`` → HP loss (shield absorbs) → emit ``Death`` if HP ≤ 0
4. ``HealApplied`` → HP gain (cap MaxHP)

User code subscribes to the same signals to react or chain new events.