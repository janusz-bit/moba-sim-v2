#pragma once

#include <memory>
#include <string>
#include <utility>

namespace moba {

struct Source;
/// Shared pointer to a `Source` node, used for provenance chain links.
using SourcePtr = std::shared_ptr<Source>;

/// Provenance chain node — tracks the origin of a modifier or event.
///
/// Each `Source` has a `name`, `description`, and optional `parent` pointer,
/// forming a linked list that records causality: "this damage came from this
/// attack, which came from this champion". The chain is walked via
/// `parent->parent->...` until `nullptr` (root).
///
/// Example:
/// ```cpp
/// auto jinx   = std::make_shared<Source>("Jinx", "champion");
/// auto attack = std::make_shared<Source>("Basic attack", "auto hit", jinx);
/// Source heal{"Bloodthirster", "lifesteal proc", attack};
/// // heal.origin()           -> "Basic attack"
/// // heal.parent->parent->name -> "Jinx"
/// ```
struct Source {
  std::string name; ///< Human-readable source name (e.g. "Bloodthirster")
  std::string description; ///< Free-form description (e.g. "lifesteal proc")
  SourcePtr parent;        ///< Parent source in the chain; `nullptr` = root

  /// Construct a Source with an explicit parent pointer.
  /// @param n Source name.
  /// @param d Description.
  /// @param p Parent source (defaults to `nullptr` = root).
  Source(std::string n = {}, std::string d = {}, SourcePtr p = {})
      : name(std::move(n)), description(std::move(d)), parent(std::move(p)) {}

  /// Convenience constructor: 3rd arg as a string creates a root parent
  /// with that name. Enables `Source{"Item", "Bloodthirster", "attacker"}`.
  /// @param n Source name.
  /// @param d Description.
  /// @param origin_name Name of the root parent to create automatically.
  Source(std::string n, std::string d, std::string origin_name)
      : name(std::move(n)), description(std::move(d)),
        parent(origin_name.empty()
                   ? SourcePtr{}
                   : std::make_shared<Source>(std::move(origin_name))) {}

  /// Returns the immediate parent's name, or empty string if this is a root.
  [[nodiscard]] std::string origin() const {
    return parent ? parent->name : std::string{};
  }

  /// Deep equality — compares name, description, and the full parent chain.
  bool operator==(const Source &o) const {
    if (name != o.name || description != o.description) {
      return false;
    }
    if (!parent && !o.parent) {
      return true;
    }
    if (!parent || !o.parent) {
      return false;
    }
    return *parent == *o.parent;
  }
};

} // namespace moba