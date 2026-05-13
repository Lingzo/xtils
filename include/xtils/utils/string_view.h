#pragma once

#include <string.h>

#include <string_view>
#include <vector>

namespace xtils {

// DEPRECATED: Use std::string_view directly.
using StringView = std::string_view;

// Case-insensitive equality comparison for string views.
inline bool CaseInsensitiveEq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  if (a.size() == 0) return true;
  return strncasecmp(a.data(), b.data(), a.size()) == 0;
}

// Returns true if |sv| case-insensitively matches any element in |others|.
inline bool CaseInsensitiveOneOf(
    std::string_view sv,
    const std::vector<std::string_view>& others) {
  for (std::string_view other : others) {
    if (CaseInsensitiveEq(sv, other)) return true;
  }
  return false;
}

// Returns true if |sv| starts with |prefix|.
inline bool StartsWith(std::string_view sv, std::string_view prefix) {
  if (prefix.size() == 0) return true;
  if (sv.size() == 0) return false;
  if (prefix.size() > sv.size()) return false;
  return memcmp(sv.data(), prefix.data(), prefix.size()) == 0;
}

// Returns true if |sv| ends with |suffix|.
inline bool EndsWith(std::string_view sv, std::string_view suffix) {
  if (suffix.size() == 0) return true;
  if (sv.size() == 0) return false;
  if (suffix.size() > sv.size()) return false;
  size_t off = sv.size() - suffix.size();
  return memcmp(sv.data() + off, suffix.data(), suffix.size()) == 0;
}

}  // namespace xtils
