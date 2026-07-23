#pragma once

#include <memory>
#include <string>
#include <utility>

namespace moba {

struct Source;
using SourcePtr = std::shared_ptr<Source>;

// Łańcuch provenance — śledzi pochodzenie modyfikatora lub eventa.
// parent wskazuje na źródło-nadrzędne, nullptr = root.
// np. Bloodthirster → Basic attack → Jinx
struct Source {
  std::string name;
  std::string description;
  SourcePtr parent;

  // Z jawnym parentem (shared_ptr).
  Source(std::string n = {}, std::string d = {}, SourcePtr p = {})
      : name(std::move(n)), description(std::move(d)), parent(std::move(p)) {}

  // Wygodne: 3. arg jako string tworzy root parent o tej nazwie.
  Source(std::string n, std::string d, std::string origin_name)
      : name(std::move(n)), description(std::move(d)),
        parent(origin_name.empty()
                   ? SourcePtr{}
                   : std::make_shared<Source>(std::move(origin_name))) {}

  // Zwraca nazwę parenta, lub "" jeśli root.
  [[nodiscard]] std::string origin() const {
    return parent ? parent->name : std::string{};
  }

  // Deep equality — porównuje cały łańcuch rekurencyjnie.
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