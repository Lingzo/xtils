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

#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>

#include "xtils/tasks/task_group.h"

namespace xtils {
#define ORDERED_EVENT(x) (x)
#define PARALLEL_PREFIX (std::int32_t)0x01000000
#define PARALLEL_EVENT(x) ((PARALLEL_PREFIX) | (x))

using EventId = std::uint32_t;
using EventData = std::any;

struct Event {
  const EventId id;
  const EventData data;

  template <typename T>
  bool is() const {
    return data.type() == typeid(T);
  }

  template <typename T>
  const T& as() const {
    return std::any_cast<const T&>(data);
  }

  template <typename T>
  std::optional<T> try_as() const {
    try {
      return std::any_cast<T>(data);
    } catch (...) {
      return std::nullopt;
    }
  }
};

using OnEvent = std::function<void(const Event&)>;

class EventManager {
 public:
  explicit EventManager(TaskGroup* tg) : tg_(tg) {}
  void connect(EventId id, OnEvent cb);
  template <typename T>
  void emit(EventId id, const T& d) {
    emit(Event{id, std::make_any<T>(d)});
  }
  void emit(const Event& e);

 private:
  using Callbacks = std::list<OnEvent>;
  // copy cbs for threads safe
  void dispatch(Callbacks cbs, const Event& e);
  std::atomic_int idx{0};
  int pool_size;
  std::unordered_map<EventId, Callbacks> maps_;
  TaskGroup* tg_;  // no own
};
}  // namespace xtils
