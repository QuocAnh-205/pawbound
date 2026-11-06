#pragma once
// Resource registry có handle (index + generation) và trạng thái loading/loaded/failed.
// Handle trỏ tới slot đã release hoặc tái sử dụng sẽ bị phát hiện là không hợp lệ.
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pb {

enum class ResourceState { Loading, Loaded, Failed };

template <class T>
struct Handle {
  uint32_t index = std::numeric_limits<uint32_t>::max();
  uint32_t generation = 0;
  friend bool operator==(Handle, Handle) = default;
};

template <class T>
class ResourceRegistry {
 public:
  // Đặt chỗ cho resource đang tải (bất đồng bộ). Trùng tên -> trả handle cũ.
  Handle<T> reserve(std::string_view name) {
    if (auto h = find(name); valid(h)) return h;
    const uint32_t idx = alloc_slot();
    Slot& s = slots_[idx];
    s.name = std::string(name);
    s.state = ResourceState::Loading;
    by_name_[s.name] = idx;
    return Handle<T>{idx, s.generation};
  }
  Handle<T> add(std::string_view name, T value) {
    Handle<T> h = reserve(name);
    set_loaded(h, std::move(value));
    return h;
  }
  bool set_loaded(Handle<T> h, T value) {
    if (!valid(h)) return false;
    Slot& s = slots_[h.index];
    s.value = std::move(value);
    s.state = ResourceState::Loaded;
    s.error.clear();
    return true;
  }
  bool set_failed(Handle<T> h, std::string error) {
    if (!valid(h)) return false;
    Slot& s = slots_[h.index];
    s.value.reset();
    s.state = ResourceState::Failed;
    s.error = std::move(error);
    return true;
  }
  bool release(Handle<T> h) {
    if (!valid(h)) return false;
    Slot& s = slots_[h.index];
    by_name_.erase(s.name);
    s.value.reset();
    s.name.clear();
    s.error.clear();
    s.used = false;
    s.generation = (s.generation == std::numeric_limits<uint32_t>::max()) ? 1 : s.generation + 1;
    free_.push_back(h.index);
    --count_;
    return true;
  }

  [[nodiscard]] bool valid(Handle<T> h) const {
    return h.index < slots_.size() && h.generation != 0 && slots_[h.index].used &&
           slots_[h.index].generation == h.generation;
  }
  [[nodiscard]] std::optional<ResourceState> state(Handle<T> h) const {
    if (!valid(h)) return std::nullopt;
    return slots_[h.index].state;
  }
  // nullptr nếu handle không hợp lệ hoặc resource chưa Loaded.
  T* get(Handle<T> h) {
    if (!valid(h) || slots_[h.index].state != ResourceState::Loaded) return nullptr;
    return &*slots_[h.index].value;
  }
  [[nodiscard]] std::string_view error(Handle<T> h) const {
    return valid(h) ? std::string_view(slots_[h.index].error) : std::string_view("handle không hợp lệ");
  }
  [[nodiscard]] Handle<T> find(std::string_view name) const {
    auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) return {};
    return Handle<T>{it->second, slots_[it->second].generation};
  }
  [[nodiscard]] size_t size() const { return count_; }

 private:
  struct Slot {
    std::optional<T> value;
    ResourceState state = ResourceState::Loading;
    std::string name;
    std::string error;
    uint32_t generation = 1;
    bool used = false;
  };
  uint32_t alloc_slot() {
    uint32_t idx;
    if (!free_.empty()) {
      idx = free_.back();
      free_.pop_back();
    } else {
      idx = static_cast<uint32_t>(slots_.size());
      slots_.emplace_back();
    }
    slots_[idx].used = true;
    ++count_;
    return idx;
  }
  std::vector<Slot> slots_;
  std::vector<uint32_t> free_;
  std::unordered_map<std::string, uint32_t> by_name_;
  size_t count_ = 0;
};

}  // namespace pb
