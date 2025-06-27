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

#include "xtils/tasks/event.h"

namespace xtils {
bool isParallelEvent(EventId id) {
  return (PARALLEL_PREFIX & id) == PARALLEL_PREFIX;
}
void EventManager::connect(EventId id, OnEvent cb) {
  tg_->PostTask([this, id, cb]() {
    auto it = maps_.find(id);
    if (it == maps_.end()) {
      maps_.insert({id, {cb}});
    } else {
      it->second.emplace_back(cb);
    }
  });
}

void EventManager::emit(const Event& e) {
  tg_->PostTask([this, e]() {
    auto it = maps_.end();
    it = maps_.find(e.id);
    if (it != maps_.end()) {
      dispatch(it->second, e);
    }
  });
}

void EventManager::dispatch(Callbacks& cbs, const Event& e) {
  if (isParallelEvent(e.id)) {
    for (auto& cb : cbs) {
      tg_->PostAsyncTask([cb, e]() { cb(e); });
    }
  } else {
    tg_->PostAsyncTask([e, cbs] {
      for (auto& cb : cbs) {
        cb(e);
      }
    });
  }
}
}  // namespace xtils
