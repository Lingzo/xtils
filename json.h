#pragma once

#include <cctype>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

namespace base {

class Json {
 public:
  using object_t = std::map<std::string, Json>;
  using array_t = std::vector<Json>;
  using string_t = std::string;
  using integer_t = int64_t;
  using float_t = double;
  using boolean_t = bool;
  using null_t = std::nullptr_t;
  using value_t = std::variant<null_t, boolean_t, integer_t, float_t, string_t,
                               array_t, object_t>;

  Json() : value_(nullptr) {}
  Json(std::nullptr_t) : value_(nullptr) {}
  Json(bool b) : value_(b) {}

  // Template constructor for integer types
  template <typename T>
  Json(T i, typename std::enable_if_t<
                std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0)
      : value_(integer_t(i)) {}

  Json(float f) : value_(float_t(f)) {}
  Json(double d) : value_(d) {}
  Json(const std::string& s) : value_(s) {}
  Json(const char* s) : value_(std::string(s)) {}
  Json(const array_t& a) : value_(a) {}
  Json(const object_t& o) : value_(o) {}

  bool is_null() const { return std::holds_alternative<null_t>(value_); }
  bool is_bool() const { return std::holds_alternative<boolean_t>(value_); }
  bool is_integer() const { return std::holds_alternative<integer_t>(value_); }
  bool is_float() const { return std::holds_alternative<float_t>(value_); }
  bool is_number() const { return is_integer() || is_float(); }
  bool is_string() const { return std::holds_alternative<string_t>(value_); }
  bool is_array() const { return std::holds_alternative<array_t>(value_); }
  bool is_object() const { return std::holds_alternative<object_t>(value_); }

  bool as_bool() const { return std::get<boolean_t>(value_); }
  int64_t as_integer() const { return std::get<integer_t>(value_); }
  double as_float() const { return std::get<float_t>(value_); }
  double as_number() const {
    if (is_integer()) return static_cast<double>(std::get<integer_t>(value_));
    if (is_float()) return std::get<float_t>(value_);
    throw std::runtime_error("Json value is not a number");
  }
  const std::string& as_string() const { return std::get<string_t>(value_); }
  const array_t& as_array() const { return std::get<array_t>(value_); }
  const object_t& as_object() const { return std::get<object_t>(value_); }

  // Non-const operator[] - creates key/index if not exists
  Json& operator[](const std::string& key) {
    if (!is_object()) value_ = object_t{};
    return std::get<object_t>(value_)[key];
  }

  // Const operator[] - throws if key doesn't exist
  const Json& operator[](const std::string& key) const {
    if (!is_object()) {
      throw std::runtime_error("Json value is not an object");
    }
    const auto& obj = std::get<object_t>(value_);
    auto it = obj.find(key);
    if (it == obj.end()) {
      throw std::runtime_error("Key '" + key + "' not found in Json object");
    }
    return it->second;
  }

  // Non-const operator[] - throws if index out of bounds
  Json& operator[](size_t i) {
    if (!is_array()) {
      throw std::runtime_error("Json value is not an array");
    }
    auto& arr = std::get<array_t>(value_);
    if (i >= arr.size()) {
      throw std::runtime_error(
          "Array index " + std::to_string(i) +
          " out of bounds (size: " + std::to_string(arr.size()) + ")");
    }
    return arr[i];
  }

  // Const operator[] - throws if index out of bounds
  const Json& operator[](size_t i) const {
    if (!is_array()) {
      throw std::runtime_error("Json value is not an array");
    }
    const auto& arr = std::get<array_t>(value_);
    if (i >= arr.size()) {
      throw std::runtime_error(
          "Array index " + std::to_string(i) +
          " out of bounds (size: " + std::to_string(arr.size()) + ")");
    }
    return arr[i];
  }

  // Safe optional access methods
  std::optional<Json> get(const std::string& key) const {
    if (!is_object()) return std::nullopt;
    const auto& obj = std::get<object_t>(value_);
    auto it = obj.find(key);
    return it != obj.end() ? std::optional<Json>(it->second) : std::nullopt;
  }

  std::optional<Json> get(size_t index) const {
    if (!is_array()) return std::nullopt;
    const auto& arr = std::get<array_t>(value_);
    return index < arr.size() ? std::optional<Json>(arr[index]) : std::nullopt;
  }

  // Key/index existence checks
  bool has_key(const std::string& key) const {
    if (!is_object()) return false;
    const auto& obj = std::get<object_t>(value_);
    return obj.find(key) != obj.end();
  }

  bool has_index(size_t index) const {
    if (!is_array()) return false;
    const auto& arr = std::get<array_t>(value_);
    return index < arr.size();
  }

  // Safe typed access methods
  std::optional<bool> get_bool(const std::string& key) const {
    auto val = get(key);
    return val && val->is_bool() ? std::optional<bool>(val->as_bool())
                                 : std::nullopt;
  }

  std::optional<int64_t> get_integer(const std::string& key) const {
    auto val = get(key);
    return val && val->is_integer() ? std::optional<int64_t>(val->as_integer())
                                    : std::nullopt;
  }

  std::optional<double> get_float(const std::string& key) const {
    auto val = get(key);
    return val && val->is_float() ? std::optional<double>(val->as_float())
                                  : std::nullopt;
  }

  std::optional<double> get_number(const std::string& key) const {
    auto val = get(key);
    return val && val->is_number() ? std::optional<double>(val->as_number())
                                   : std::nullopt;
  }

  std::optional<std::string> get_string(const std::string& key) const {
    auto val = get(key);
    return val && val->is_string()
               ? std::optional<std::string>(val->as_string())
               : std::nullopt;
  }

  std::optional<array_t> get_array(const std::string& key) const {
    auto val = get(key);
    return val && val->is_array() ? std::optional<array_t>(val->as_array())
                                  : std::nullopt;
  }

  std::optional<object_t> get_object(const std::string& key) const {
    auto val = get(key);
    return val && val->is_object() ? std::optional<object_t>(val->as_object())
                                   : std::nullopt;
  }

  // Array typed access methods
  std::optional<bool> get_bool(size_t index) const {
    auto val = get(index);
    return val && val->is_bool() ? std::optional<bool>(val->as_bool())
                                 : std::nullopt;
  }

  std::optional<int64_t> get_integer(size_t index) const {
    auto val = get(index);
    return val && val->is_integer() ? std::optional<int64_t>(val->as_integer())
                                    : std::nullopt;
  }

  std::optional<double> get_float(size_t index) const {
    auto val = get(index);
    return val && val->is_float() ? std::optional<double>(val->as_float())
                                  : std::nullopt;
  }

  std::optional<double> get_number(size_t index) const {
    auto val = get(index);
    return val && val->is_number() ? std::optional<double>(val->as_number())
                                   : std::nullopt;
  }

  std::optional<std::string> get_string(size_t index) const {
    auto val = get(index);
    return val && val->is_string()
               ? std::optional<std::string>(val->as_string())
               : std::nullopt;
  }

  std::optional<array_t> get_array(size_t index) const {
    auto val = get(index);
    return val && val->is_array() ? std::optional<array_t>(val->as_array())
                                  : std::nullopt;
  }

  std::optional<object_t> get_object(size_t index) const {
    auto val = get(index);
    return val && val->is_object() ? std::optional<object_t>(val->as_object())
                                   : std::nullopt;
  }

  // Size information
  size_t size() const {
    if (is_array()) return std::get<array_t>(value_).size();
    if (is_object()) return std::get<object_t>(value_).size();
    return 0;
  }

  bool empty() const {
    if (is_array()) return std::get<array_t>(value_).empty();
    if (is_object()) return std::get<object_t>(value_).empty();
    if (is_string()) return std::get<string_t>(value_).empty();
    return is_null();
  }

  std::string dump(int indent = 0) const;
  static Json parse(const std::string& text, std::error_code& ec);
  static std::optional<Json> parse(const std::string& text);

 private:
  value_t value_;
};

}  // namespace base
