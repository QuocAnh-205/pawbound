#pragma once
// ECS tối thiểu: sparse-set mỗi loại component, entity = (index, generation).
//
// Trade-off: sparse set cho add/remove/get O(1) và duyệt dense array liền mạch theo từng loại component.
// Không tối ưu cho query nhiều component bằng archetype; với quy mô PAWBOUND (vài nghìn entity/server)
// cách này đủ nhanh và dễ debug hơn nhiều.
//
// Quy tắc: không chạy trên nhiều thread. Trong `each<...>`, được phép destroy/remove entity ĐANG duyệt
// (duyệt ngược), nhưng không được add component thuộc các loại đang duyệt.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pb::ecs {

inline constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

struct Entity {
  uint32_t index = kInvalidIndex;
  uint32_t generation = 0;  // generation 0 không bao giờ hợp lệ
  friend bool operator==(Entity, Entity) = default;
};

namespace detail {
inline uint32_t next_type_id() {
  static uint32_t counter = 0;
  return counter++;
}
template <class T>
uint32_t type_id() {
  static const uint32_t id = next_type_id();
  return id;
}

class IPool {
 public:
  virtual ~IPool() = default;
  virtual bool remove(uint32_t index) = 0;
  [[nodiscard]] virtual bool contains(uint32_t index) const = 0;
  [[nodiscard]] virtual size_t size() const = 0;
  [[nodiscard]] virtual const std::vector<uint32_t>& entities() const = 0;
};

template <class T>
class Pool final : public IPool {
 public:
  template <class... Args>
  T& emplace(uint32_t index, Args&&... args) {
    if (index >= sparse_.size()) sparse_.resize(static_cast<size_t>(index) + 1, kInvalidIndex);
    if (sparse_[index] != kInvalidIndex) {
      data_[sparse_[index]] = T{std::forward<Args>(args)...};
      return data_[sparse_[index]];
    }
    sparse_[index] = static_cast<uint32_t>(dense_.size());
    dense_.push_back(index);
    data_.push_back(T{std::forward<Args>(args)...});
    return data_.back();
  }
  bool remove(uint32_t index) override {
    if (!contains(index)) return false;
    const uint32_t pos = sparse_[index];
    const uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
    if (pos != last) {
      dense_[pos] = dense_[last];
      data_[pos] = std::move(data_[last]);
      sparse_[dense_[pos]] = pos;
    }
    dense_.pop_back();
    data_.pop_back();
    sparse_[index] = kInvalidIndex;
    return true;
  }
  [[nodiscard]] bool contains(uint32_t index) const override {
    return index < sparse_.size() && sparse_[index] != kInvalidIndex;
  }
  [[nodiscard]] size_t size() const override { return dense_.size(); }
  [[nodiscard]] const std::vector<uint32_t>& entities() const override { return dense_; }
  T* get(uint32_t index) { return contains(index) ? &data_[sparse_[index]] : nullptr; }
  const T* get(uint32_t index) const { return contains(index) ? &data_[sparse_[index]] : nullptr; }

 private:
  std::vector<uint32_t> sparse_;
  std::vector<uint32_t> dense_;
  std::vector<T> data_;
};
}  // namespace detail

class Registry {
 public:
  Entity create() {
    uint32_t index;
    if (!free_.empty()) {
      index = free_.back();
      free_.pop_back();
    } else {
      index = static_cast<uint32_t>(generations_.size());
      generations_.push_back(1);
    }
    ++alive_;
    return Entity{index, generations_[index]};
  }

  bool destroy(Entity e) {
    if (!valid(e)) return false;
    for (auto& p : pools_)
      if (p) p->remove(e.index);
    uint32_t& g = generations_[e.index];
    g = (g == std::numeric_limits<uint32_t>::max()) ? 1 : g + 1;
    free_.push_back(e.index);
    --alive_;
    return true;
  }

  [[nodiscard]] bool valid(Entity e) const {
    return e.index < generations_.size() && e.generation != 0 && generations_[e.index] == e.generation;
  }
  [[nodiscard]] size_t alive() const { return alive_; }

  template <class T, class... Args>
  T& add(Entity e, Args&&... args) {
    require_valid(e);
    return pool<T>().emplace(e.index, std::forward<Args>(args)...);
  }
  template <class T>
  bool remove(Entity e) {
    if (!valid(e)) return false;
    auto* p = find_pool<T>();
    return p && p->remove(e.index);
  }
  template <class T>
  [[nodiscard]] bool has(Entity e) const {
    const auto* p = find_pool<T>();
    return valid(e) && p && p->contains(e.index);
  }
  template <class T>
  T* try_get(Entity e) {
    if (!valid(e)) return nullptr;
    auto* p = find_pool<T>();
    return p ? p->get(e.index) : nullptr;
  }
  template <class T>
  T& get(Entity e) {
    T* c = try_get<T>(e);
    if (!c) throw std::logic_error("ecs: entity không có component yêu cầu hoặc không hợp lệ");
    return *c;
  }
  template <class T>
  [[nodiscard]] size_t count() const {
    const auto* p = find_pool<T>();
    return p ? p->size() : 0;
  }

  // Gọi f(Entity, Ts&...) cho mọi entity có đủ các component Ts.
  template <class... Ts, class F>
  void each(F&& f) {
    static_assert(sizeof...(Ts) > 0);
    detail::IPool* smallest = nullptr;
    bool missing = false;
    ((check_smallest<Ts>(smallest, missing)), ...);
    if (missing || !smallest) return;
    const auto& ents = smallest->entities();
    for (size_t i = ents.size(); i-- > 0;) {
      if (i >= ents.size()) continue;  // phần tử bị xóa trong callback trước
      const uint32_t idx = ents[i];
      if (!(find_pool<Ts>()->contains(idx) && ...)) continue;
      f(Entity{idx, generations_[idx]}, *find_pool<Ts>()->get(idx)...);
    }
  }

 private:
  void require_valid(Entity e) const {
    if (!valid(e)) throw std::logic_error("ecs: entity không hợp lệ (đã bị destroy hoặc chưa tạo)");
  }
  template <class T>
  detail::Pool<T>& pool() {
    const uint32_t id = detail::type_id<T>();
    if (id >= pools_.size()) pools_.resize(static_cast<size_t>(id) + 1);
    if (!pools_[id]) pools_[id] = std::make_unique<detail::Pool<T>>();
    return static_cast<detail::Pool<T>&>(*pools_[id]);
  }
  template <class T>
  detail::Pool<T>* find_pool() {
    const uint32_t id = detail::type_id<T>();
    return id < pools_.size() ? static_cast<detail::Pool<T>*>(pools_[id].get()) : nullptr;
  }
  template <class T>
  const detail::Pool<T>* find_pool() const {
    const uint32_t id = detail::type_id<T>();
    return id < pools_.size() ? static_cast<const detail::Pool<T>*>(pools_[id].get()) : nullptr;
  }
  template <class T>
  void check_smallest(detail::IPool*& smallest, bool& missing) {
    detail::IPool* p = find_pool<T>();
    if (!p) {
      missing = true;
      return;
    }
    if (!smallest || p->size() < smallest->size()) smallest = p;
  }

  std::vector<uint32_t> generations_;
  std::vector<uint32_t> free_;
  std::vector<std::unique_ptr<detail::IPool>> pools_;
  size_t alive_ = 0;
};

}  // namespace pb::ecs
