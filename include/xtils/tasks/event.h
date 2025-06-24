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
#include <memory>
#include <unordered_map>

#include "xtils/tasks/task_group.h"

namespace xtils {
#define ORDERED_EVENT(x) (x)
#define PARALLEL_PREFIX (std::int32_t)0x01000000
#define PARALLEL_EVENT(x) ((PARALLEL_PREFIX) | (x))

using EventId = std::uint32_t;
using EventData = std::shared_ptr<void>;

struct Event {
  explicit Event(EventId e, EventData d = nullptr) : id(e), data(d) {}

  template <typename T>
  const T& refData() const {
    return *std::static_pointer_cast<T>(this->data);
  }
  const EventId id;
  const EventData data;
};

using OnEvent = std::function<void(const Event&)>;

class EventManager {
 public:
  explicit EventManager(TaskGroup& tg)
      : tg_(tg) {}
  void connect(EventId id, OnEvent cb);
  template <typename T>
  void emit(EventId id, const T& d) {
    emit(Event{id, std::make_shared<T>(d)});
  }
  void emit(const Event& e);

 private:
  using Callbacks = std::list<OnEvent>;
  void dispatch(Callbacks& cbs, const Event& e);
  std::atomic_int idx{0};
  int pool_size;
  std::unordered_map<EventId, Callbacks> maps_;
  TaskGroup& tg_;
};
}  // namespace xtils
