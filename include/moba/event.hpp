#pragma once

#include "moba/signal.hpp"
#include "moba/source.hpp"
#include "moba/types.hpp"

#include <cstddef>
#include <variant>

namespace moba {

// Event: atak trafia cel. Simulation konwertuje to na DamageReceived +
// DamageDealt.
struct AttackHit {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{}; // raw, pre-mitigation
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

// Event: obrażenia zadane. Wewnętrzny handler używa tego do lifesteal/omnivamp.
struct DamageDealt {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{}; // post-mitigation
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

// Event: obrażenia otrzymane. Wewnętrzny handler aplikuje HP loss + death
// check.
struct DamageReceived {
  std::size_t actor_id{};
  std::size_t target_id{};
  Type amount{}; // post-mitigation
  TypeDamage damage_type{};
  Source source;
  Type time{};
};

// Event: heal. Wewnętrzny handler aplikuje HP gain (cap MaxHP).
struct HealApplied {
  std::size_t target_id{};
  Type amount{};
  Source source;
  Type time{};
};

// Event: champion umarł (HP ≤ 0). Emitowany z DamageReceived handlera.
struct Death {
  std::size_t actor_id{};  // killer
  std::size_t target_id{}; // victim
  Source source;
  Type time{};
};

// Variant wszystkich eventów + monostate. monostate = normalna ewaluacja statów
// (bez eventa). Pasywa otrzymują to jako 4. argument.
using PassiveEvent = std::variant<std::monostate, AttackHit, DamageDealt,
                                  DamageReceived, HealApplied, Death>;

} // namespace moba