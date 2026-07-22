Python Bindings
================

The Python API mirrors the C++ high-level interface via `nanobind
<https://github.com/wjakob/nanobind>`_.  The package is built as a Nix
derivation and available in the dev shell via ``python3.withPackages`` —
no ``PYTHONPATH`` or ``pip install`` needed.

Installation
------------

.. code-block:: sh

   nix develop
   python3 -c "from moba import Champion; print('ok')"

Quick Start
-----------

.. code-block:: python

   from moba import Champion, Simulation, Stat, ModType, TypeDamage, Source

   # Static champion with a permanent passive
   c = Champion({Stat.MaxHP: 1000, Stat.AD: 50, Stat.AR: 100})
   c.add_passive(lambda base, final, time: [(Stat.AD, ModType.Base, 10.0)])
   print(c.evaluate_champion()[Stat.AD])  # 60.0

   # Simulation with events (lifesteal is built-in)
   sim = Simulation()
   sim.add_champion(Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 800,
                              Stat.AD: 100, Stat.LifeSteal: 0.12}))
   sim.add_champion(Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000,
                              Stat.AR: 100}))
   sim.emit_attack_hit(0, 1, 100.0, TypeDamage.Physical,
                       Source("Basic attack"), 0.0)
   print(sim.get_champion(0).get_base_stats()[Stat.CurrentHP])  # 806.0

API Summary
-----------

==============================  ==============================================
Class / Function                Description
==============================  ==============================================
``Champion(stats_dict)``        Create a champion from ``{Stat: value}``
``champion.add_passive(cb)``    Add a passive (Python callable)
``champion.evaluate_champion()`` Fixed-point evaluation, returns ``ndarray``
``champion.get_base_stats()``   Stats from mod_db, returns ``ndarray``
``Simulation()``                Create a simulation
``sim.add_champion(champ)``     Add a champion to the sim
``sim.get_champion(i)``         Get champion by index
``sim.emit_attack_hit(...)``    Emit an AttackHit event
``sim.emit_heal_applied(...)``  Emit a HealApplied event
``sim.on_*_subscribe(cb)``      Subscribe to a signal
``sim.clear_signals()``         Break reference cycles
``mitigated_damage(...)``       Compute post-mitigation damage
``apply_damage_to_shield(...)`` Shield absorption calculation
``get_stat(stats, stat)``       Read a stat from an ndarray
``Source(name, desc, parent)``  Provenance chain node
==============================  ==============================================

Passive Callbacks
-----------------

Passive callbacks receive ``(base, final, time)`` where ``base`` and ``final``
are ``numpy.ndarray`` (float64, shape ``[25]``) and ``time`` is a float.

Return value can be:

- **List of tuples**: ``[(Stat, ModType, value, [Source]), ...]``
  — passive stays alive (``alive=True``).
- **Dict**: ``{"mods": [...], "alive": bool}`` — explicit lifetime control.

.. code-block:: python

   # Permanent +10 AD
   c.add_passive(lambda b, f, t: [(Stat.AD, ModType.Base, 10.0)])

   # One-shot +25 AD (consumed after one evaluation)
   c.add_passive(lambda b, f, t: {"mods": [(Stat.AD, ModType.Base, 25.0)],
                                  "alive": False})

   # Temp passive (expires after 3 seconds)
   def burn(b, f, t):
       if t < 3.0:
           return [(Stat.CurrentHP, ModType.Base, -20.0)]
       return {"mods": [], "alive": False}
   c.add_passive(burn)

Notes
-----

- ``TypeDamage.True_`` is used instead of ``True`` (Python keyword).
- ``Stat`` enum values support ``__index__`` for numpy array indexing.
- Call ``sim.clear_signals()`` before discarding a ``Simulation`` to break
  reference cycles (nanobind types don't participate in Python's cyclic GC).