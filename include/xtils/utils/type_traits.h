#pragma once

#include <string_view>

namespace xtils {

template <typename T>
constexpr std::string_view type_name() {
#if defined(__clang__)
  std::string_view p = __PRETTY_FUNCTION__;
  auto start = p.find("T = ") + 4;
  auto end = p.find(']', start);
  return p.substr(start, end - start);
#elif defined(__GNUC__)
  std::string_view p = __PRETTY_FUNCTION__;
  auto start = p.find("with T = ") + 8;
  auto end = p.find(';', start);
  return p.substr(start, end - start);
#elif defined(_MSC_VER)
  std::string_view p = __FUNCSIG__;
  auto start = p.find("type_name<") + 10;
  auto end = p.find(">(void)", start);
  return p.substr(start, end - start);
#endif
}

template <typename T>
constexpr std::string_view pretty_name() {
  using U = std::remove_cv_t<std::remove_reference_t<T>>;
  return type_name<U>();
}

}  // namespace xtils
