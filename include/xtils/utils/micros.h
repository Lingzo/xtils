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

#define SAFE_DELETE_OBJ(x) \
  do {                     \
    if (x) {               \
      delete x;            \
      x = nullptr;         \
    }                      \
  } while (0)
