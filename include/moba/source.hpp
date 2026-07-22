#pragma once

#include <memory>
#include <string>
#include <utility>

namespace moba {

struct Source;
using SourcePtr = std::shared_ptr<Source>;

struct Source {
  std::string name;
  std::string description;
  SourcePtr parent; // chainable provenance; nullptr = root

  Source(std::string n = {}, std::string d = {}, SourcePtr p = {})
      : name(std::move(n)), description(std::move(d)), parent(std::move(p)) {}

  // Convenience: 3rd arg as string -> creates a root parent with that name.
  Source(std::string n, std::string d, std::string origin_name)
      : name(std::move(n)), description(std::move(d)),
        parent(origin_name.empty()
                   ? SourcePtr{}
                   : std::make_shared<Source>(std::move(origin_name))) {}

  [[nodiscard]] std::string origin() const {
    return parent ? parent->name : std::string{};
  }

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