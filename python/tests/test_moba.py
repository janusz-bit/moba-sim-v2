import numpy as np
import pytest

from moba import (
    Champion,
    ModType,
    Simulation,
    Source,
    Stat,
    TypeDamage,
    apply_damage_to_shield,
    get_stat,
    mitigated_damage,
    post_mitigation_damage,
)


# --- Champion basics ---

def test_champion_empty():
    c = Champion()
    s = c.get_base_stats()
    assert s.shape == (25,)
    assert s[Stat.AD] == pytest.approx(0.0)


def test_champion_init_dict():
    c = Champion({Stat.MaxHP: 1000, Stat.AD: 50, Stat.AR: 100})
    s = c.get_base_stats()
    assert s[Stat.MaxHP] == pytest.approx(1000.0)
    assert s[Stat.AD] == pytest.approx(50.0)
    assert s[Stat.AR] == pytest.approx(100.0)


def test_evaluate_champion_no_passives():
    c = Champion({Stat.AD: 50})
    r = c.evaluate_champion()
    assert r[Stat.AD] == pytest.approx(50.0)


def test_evaluate_champion_permanent_passive():
    c = Champion({Stat.AD: 50})

    def bonus(base, final, time):
        return [(Stat.AD, ModType.Base, 10.0)]

    c.add_passive(bonus)
    r = c.evaluate_champion()
    assert r[Stat.AD] == pytest.approx(60.0)


def test_passive_dict_result():
    c = Champion({Stat.AD: 50})

    def bonus(base, final, time):
        return {"mods": [(Stat.AD, ModType.Base, 25.0)], "alive": False}

    c.add_passive(bonus)
    r = c.evaluate_champion()
    assert r[Stat.AD] == pytest.approx(75.0)
    # one-shot consumed
    assert c.passives_count == 0


# --- Combat helpers ---

def test_mitigated_damage_physical():
    target = Champion({Stat.AR: 100}).get_base_stats()
    # 100 physical vs 100 AR -> 50
    dealt = mitigated_damage(100.0, TypeDamage.Physical, target)
    assert dealt == pytest.approx(50.0)


def test_mitigated_damage_true():
    target = Champion({Stat.AR: 1000}).get_base_stats()
    dealt = mitigated_damage(100.0, TypeDamage.True_, target)
    assert dealt == pytest.approx(100.0)


def test_post_mitigation_damage_negative_armor():
    # 100 vs -100 AR -> 150
    assert post_mitigation_damage(100.0, -100.0) == pytest.approx(150.0)


def test_apply_damage_to_shield_absorbs():
    sh, hp = apply_damage_to_shield(200.0, 1000.0, 50.0)
    assert sh == pytest.approx(150.0)
    assert hp == pytest.approx(1000.0)


def test_apply_damage_to_shield_overflow():
    sh, hp = apply_damage_to_shield(50.0, 1000.0, 200.0)
    assert sh == pytest.approx(0.0)
    assert hp == pytest.approx(850.0)


# --- Simulation events ---

def test_attack_hit_deals_damage():
    sim = Simulation()
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AD: 100})
    )
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AR: 100})
    )

    sim.emit_attack_hit(0, 1, 100.0, TypeDamage.Physical,
                        Source("Basic attack"), 0.0)

    target = sim.get_champion(1).get_base_stats()
    assert target[Stat.CurrentHP] == pytest.approx(950.0)


def test_lifesteal_via_signal():
    sim = Simulation()
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 800,
                  Stat.AD: 100, Stat.LifeSteal: 0.12})
    )
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AR: 100})
    )

    def lifesteal(ev):
        atk = sim.get_champion(ev.actor_id).get_base_stats()
        heal = ev.amount * atk[Stat.LifeSteal]
        sim.emit_heal_applied(ev.actor_id, heal, Source("Lifesteal"), ev.time)

    sim.on_damage_dealt_subscribe(lifesteal)

    sim.emit_attack_hit(0, 1, 100.0, TypeDamage.Physical,
                        Source("Basic attack"), 0.0)

    atk = sim.get_champion(0).get_base_stats()
    assert atk[Stat.CurrentHP] == pytest.approx(806.0)  # 800 + 6 (50 * 0.12)
    tgt = sim.get_champion(1).get_base_stats()
    assert tgt[Stat.CurrentHP] == pytest.approx(950.0)


def test_death_event_fires():
    sim = Simulation()
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AD: 999})
    )
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 100})
    )

    death_seen = []

    def on_death(ev):
        death_seen.append(ev)

    sim.on_death_subscribe(on_death)

    sim.emit_attack_hit(0, 1, 999.0, TypeDamage.True_, Source("Execute"), 0.0)

    assert len(death_seen) == 1
    assert death_seen[0].target_id == 1
    assert death_seen[0].actor_id == 0


def test_true_damage_bypasses_mitigation():
    sim = Simulation()
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AD: 100})
    )
    sim.add_champion(
        Champion({Stat.MaxHP: 1000, Stat.CurrentHP: 1000, Stat.AR: 500})
    )

    sim.emit_attack_hit(0, 1, 100.0, TypeDamage.True_, Source("True"), 0.0)

    t = sim.get_champion(1).get_base_stats()
    assert t[Stat.CurrentHP] == pytest.approx(900.0)


# --- Source chain ---

def test_source_chain():
    jinx = Source("Jinx", "champion")
    attack = Source("Basic attack", "auto", jinx)
    heal = Source("Bloodthirster", "lifesteal", attack)
    assert heal.origin() == "Basic attack"
    assert heal.parent.parent.name == "Jinx"


# --- get_stat helper ---

def test_get_stat_helper():
    c = Champion({Stat.AD: 80, Stat.AR: 60})
    s = c.get_base_stats()
    assert get_stat(s, Stat.AD) == pytest.approx(80.0)
    assert get_stat(s, Stat.AR) == pytest.approx(60.0)


# --- Stats as numpy array ---

def test_stats_is_numpy_array():
    c = Champion({Stat.AD: 50})
    s = c.get_base_stats()
    assert isinstance(s, np.ndarray)
    assert s.dtype == np.float64
    # Slicing works: AD=5, AR=7 -> slice [5:8] has 3 elements
    assert s[int(Stat.AD):int(Stat.AR) + 1].shape == (3,)