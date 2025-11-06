/*
 * Description: DOC
 *
 * Copyright (c) 2018 - 2024 Albert Lv <altair.albert@gmail.com>
 *
 * Licensed under the MIT License. See LICENSE file in the project root for full
 * license information.
 *
 * Author: Albert Lv <altair.albert@gmail.com>
 * Version: 0.0.0
 *
 * Changelog:
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <typeindex>
#include <unordered_map>

#include "xtils/tasks/task_group.h"
#include "xtils/utils/type_traits.h"

namespace xtils {

using EventId = std::uint32_t;

struct IEvent {
  virtual std::string_view name() = 0;
};

template <typename T>
struct Event : public IEvent {
  std::string_view name() override { return xtils::type_name<T>(); }
};

using OnEvent = std::function<void(const void*)>;

class EventManager {
 public:
  explicit EventManager(TaskGroup* tg) : tg_(tg) {}

  template <typename EventT>
  using TypedCallback = std::function<void(const EventT&)>;

  // Use enable_if as a non-type template parameter (int = 0) so the
  // function templates have distinct signatures and do not collide.
  template <typename T, std::enable_if_t<!std::is_enum_v<T>, int> = 0>
  void connect(TypedCallback<T> cb) {
    std::type_index type = std::type_index(typeid(T));
    maps_[type].emplace_back(
        [cb](const void* e) { cb(*static_cast<const T*>(e)); });
  }

  template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
  void connect(T id, TypedCallback<T> cb) {
    std::uint32_t uid = static_cast<std::uint32_t>(id);
    enum_maps_[uid].emplace_back(
        [cb](const void* e) { cb(*static_cast<const T*>(e)); });
  }

  template <typename T, std::enable_if_t<!std::is_enum_v<T>, int> = 0>
  void emit(const T& e) {
    std::type_index type = std::type_index(typeid(T));
    auto it = maps_.find(type);
    if (it != maps_.end()) {
      dispatch(it->second, e);
    }
  }

  template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
  void emit(const T& e) {
    std::uint32_t uid = static_cast<std::uint32_t>(e);
    auto it = enum_maps_.find(uid);
    if (it != enum_maps_.end()) {
      dispatch(it->second, e);
    }
  }

 private:
  using Callbacks = std::list<OnEvent>;
  template <typename T>
  // copy cbs for threads safe
  void dispatch(Callbacks cbs, const T& e) {
    tg_->PostAsyncTask([e, cbs] {
      for (auto& cb : cbs) {
        cb(&e);
      }
    });
  }

 private:
  std::atomic_int idx{0};
  int pool_size;
  std::unordered_map<std::type_index, Callbacks> maps_;
  std::map<std::uint32_t, Callbacks> enum_maps_;
  TaskGroup* tg_;  // no own
};
}  // namespace xtils
