#pragma once

#include "moba/signal.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>
#include <variant>

namespace moba {

struct AttackHit {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

struct DamageDealt {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

struct DamageReceived {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{};
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

struct HealApplied {
  std::size_t target_id{};
  Type amount{};
  Source source;
  Type time{};
};

struct Death {
  std::size_t actor_id{};
  std::size_t target_id{};
  Source source;
  Type time{};
};

using PassiveEvent = std::variant<std::monostate, AttackHit, DamageDealt,
                                  DamageReceived, HealApplied, Death>;

} // namespace moba