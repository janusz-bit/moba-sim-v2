#pragma once

namespace moba {
using Type = double;

// Post-mitigation physical damage after armor reduction.
// See: https://wiki.leagueoflegends.com/en-us/Armor
Type post_mitigation_damage(const Type &raw_damage,
                            const Type &resistanse) noexcept;

} // namespace moba
