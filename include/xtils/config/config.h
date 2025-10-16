#pragma once

#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "xtils/utils/json.h"

namespace xtils {

class Config {
 public:
  // Option definition for command line parsing
  struct Option {
    std::string name;
    std::string description;
    Json default_value;
    bool required = false;

    Option() = default;
    Option(const std::string& name, const std::string& description,
           const Json& default_value, bool required = false)
        : name(name),
          description(description),
          default_value(default_value),
          required(required) {}
    Option& operator=(const Option& o) {
      this->name = o.name;
      this->description = o.description;
      this->default_value = o.default_value;
      this->required = o.required;
      return *this;
    }
  };

  Config() : data_(Json::object_t{}) {}

  // Configuration definition
  Config& define(const std::string& name, const std::string& description,
                 const Json& default_value, bool required = false);

  // Template version for C++ native types
  template <typename T>
  Config& define(const std::string& name, const std::string& description,
                 const T& default_value, bool required = false);

  // Loading methods
  // parse_args supports --config-file parameter to load configuration file
  // first, then command line arguments can override file settings
  bool parse_args(int argc, const char** argv, bool allow_exit = false);
  bool parse_args(const std::vector<std::string>& args,
                  bool allow_exit = false);
  bool load_file(const std::string& filename);
  bool parse_json(const Json& json);
  bool parse(const std::string& json_content);

  // Primary access method with dot notation support (e.g., "server.port")
  template <typename T>
  T get(const std::string& path) const;

  // Specialized getters for common types
  std::string get_string(const std::string& path) const;
  int64_t get_int(const std::string& path) const;
  double get_double(const std::string& path) const;
  bool get_bool(const std::string& path) const;
  std::optional<Json> get(const std::string& path) const;

  // Utility methods
  bool has(const std::string& path) const;
  void set(const std::string& path, const Json& value);

  // Template version for C++ native types
  template <typename T>
  void set(const std::string& path, const T& value);

  // Validation
  bool validate() const;
  std::string help() const;
  std::vector<std::string> missing_required() const;
  std::vector<std::string> no_parsed() const;

  // Serialization
  std::string to_string() const;
  Json to_json() const;
  bool save(const std::string& filename) const;
  void print() const;
  auto options() { return options_; }

 private:
  std::map<std::string, Option> options_;
  Json data_;

  // Helper methods
  std::optional<Json> parse_value(const std::string& value_str,
                                  const Json& default_value) const;
  void apply_defaults();
  std::vector<std::string> split_path(const std::string& path) const;
  Json merge_objects(const Json& xtils, const Json& overlay) const;
  std::vector<std::string> no_parsed_;
};

// Template implementation
template <typename T>
T Config::get(const std::string& path) const {
  auto json_val = get(path);
  if (!json_val) {
    throw std::runtime_error("key not find");
  }

  try {
    if constexpr (std::is_same_v<T, std::string>) {
      return json_val->is_string()
                 ? json_val->as_string()
                 : throw std::runtime_error("value not string");
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return json_val->is_integer()
                 ? json_val->as_integer()
                 : throw std::runtime_error("value not integer");
    } else if constexpr (std::is_same_v<T, double>) {
      if (json_val->is_float()) return json_val->as_float();
      if (json_val->is_integer())
        return static_cast<double>(json_val->as_integer());
    } else if constexpr (std::is_same_v<T, bool>) {
      return json_val->is_bool() ? json_val->as_bool()
                                 : throw std::runtime_error("value not bool");
    } else if constexpr (std::is_integral_v<T>) {
      return json_val->is_integer()
                 ? static_cast<T>(json_val->as_integer())
                 : throw std::runtime_error("value not integral");
    } else if constexpr (std::is_floating_point_v<T>) {
      if (json_val->is_float()) return static_cast<T>(json_val->as_float());
      if (json_val->is_integer()) return static_cast<T>(json_val->as_integer());
      throw std::runtime_error("value not float");
    } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
      if (!json_val->is_array()) throw std::runtime_error("value not a array");
      std::vector<int64_t> result;
      auto array = json_val->as_array();
      for (const auto& val : array) {
        if (val.is_integer()) {
          result.push_back(val.as_integer());
        } else if (val.is_float()) {
          result.push_back(static_cast<int64_t>(val.as_float()));
        }
      }
      return result;
    } else if constexpr (std::is_same_v<T, std::vector<double>>) {
      if (!json_val->is_array()) throw std::runtime_error("value not a array");
      std::vector<double> result;
      auto array = json_val->as_array();
      for (const auto& val : array) {
        if (val.is_float()) {
          result.push_back(val.as_float());
        } else if (val.is_integer()) {
          result.push_back(static_cast<double>(val.as_integer()));
        }
      }
      return result;
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      if (!json_val->is_array()) throw std::runtime_error("value not a array");
      std::vector<std::string> result;
      auto array = json_val->as_array();
      for (const auto& val : array) {
        if (val.is_string()) {
          result.push_back(val.as_string());
        }
      }
      return result;
    } else if constexpr (std::is_same_v<T, std::vector<int>>) {
      if (!json_val->is_array()) throw std::runtime_error("value not a array");
      std::vector<int> result;
      auto array = json_val->as_array();
      for (const auto& val : array) {
        if (val.is_integer()) {
          result.push_back(static_cast<int>(val.as_integer()));
        } else if (val.is_float()) {
          result.push_back(static_cast<int>(val.as_float()));
        }
      }
      return result;
    } else if constexpr (std::is_same_v<T, std::vector<float>>) {
      if (!json_val->is_array()) throw std::runtime_error("value not a array");
      std::vector<float> result;
      auto array = json_val->as_array();
      for (const auto& val : array) {
        if (val.is_float()) {
          result.push_back(static_cast<float>(val.as_float()));
        } else if (val.is_integer()) {
          result.push_back(static_cast<float>(val.as_integer()));
        }
      }
      return result;
    } else if constexpr (std::is_same_v<T, Json>) {
      return *json_val;
    } else {
      static_assert(
          std::is_same_v<T, std::string> || std::is_same_v<T, int64_t> ||
              std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_integral_v<T> || std::is_floating_point_v<T> ||
              std::is_same_v<T, std::vector<int64_t>> ||
              std::is_same_v<T, std::vector<double>> ||
              std::is_same_v<T, std::vector<std::string>> ||
              std::is_same_v<T, std::vector<int>> ||
              std::is_same_v<T, std::vector<float>> || std::is_same_v<T, Json>,
          "Unsupported type for Config::get");
      throw std::runtime_error("value not a array");
    }
  } catch (const std::exception e) {
    throw e;
  }
  // can't be here
  return T{};
}

// Template implementation for define with native types
template <typename T>
Config& Config::define(const std::string& name, const std::string& description,
                       const T& default_value, bool required) {
  Json json_value;
  if constexpr (std::is_same_v<T, std::string>) {
    json_value = Json(default_value);
  } else if constexpr (std::is_same_v<T, const char*> || std::is_array_v<T>) {
    json_value = Json(std::string(default_value));
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    json_value = Json(static_cast<int64_t>(default_value));
  } else if constexpr (std::is_floating_point_v<T>) {
    json_value = Json(static_cast<double>(default_value));
  } else if constexpr (std::is_same_v<T, bool>) {
    json_value = Json(default_value);
  } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
    Json::array_t arr;
    for (const auto& val : default_value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<double>>) {
    Json::array_t arr;
    for (const auto& val : default_value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    Json::array_t arr;
    for (const auto& val : default_value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<int>>) {
    Json::array_t arr;
    for (const auto& val : default_value) {
      arr.push_back(Json(static_cast<int64_t>(val)));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<float>>) {
    Json::array_t arr;
    for (const auto& val : default_value) {
      arr.push_back(Json(static_cast<double>(val)));
    }
    json_value = Json(arr);
  } else {
    static_assert(std::is_same_v<T, std::string> ||
                      std::is_same_v<T, const char*> || std::is_array_v<T> ||
                      std::is_integral_v<T> || std::is_floating_point_v<T> ||
                      std::is_same_v<T, bool> ||
                      std::is_same_v<T, std::vector<int64_t>> ||
                      std::is_same_v<T, std::vector<double>> ||
                      std::is_same_v<T, std::vector<std::string>> ||
                      std::is_same_v<T, std::vector<int>> ||
                      std::is_same_v<T, std::vector<float>>,
                  "Unsupported type for Config::define");
  }
  return define(name, description, json_value, required);
}

// Template implementation for set with native types
template <typename T>
void Config::set(const std::string& path, const T& value) {
  Json json_value;
  if constexpr (std::is_same_v<T, std::string>) {
    json_value = Json(value);
  } else if constexpr (std::is_same_v<T, const char*> || std::is_array_v<T>) {
    json_value = Json(std::string(value));
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    json_value = Json(static_cast<int64_t>(value));
  } else if constexpr (std::is_floating_point_v<T>) {
    json_value = Json(static_cast<double>(value));
  } else if constexpr (std::is_same_v<T, bool>) {
    json_value = Json(value);
  } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<double>>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(Json(val));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<int>>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(Json(static_cast<int64_t>(val)));
    }
    json_value = Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<float>>) {
    Json::array_t arr;
    for (const auto& val : value) {
      arr.push_back(Json(static_cast<double>(val)));
    }
    json_value = Json(arr);
  } else {
    static_assert(std::is_same_v<T, std::string> ||
                      std::is_same_v<T, const char*> || std::is_array_v<T> ||
                      std::is_integral_v<T> || std::is_floating_point_v<T> ||
                      std::is_same_v<T, bool> ||
                      std::is_same_v<T, std::vector<int64_t>> ||
                      std::is_same_v<T, std::vector<double>> ||
                      std::is_same_v<T, std::vector<std::string>> ||
                      std::is_same_v<T, std::vector<int>> ||
                      std::is_same_v<T, std::vector<float>>,
                  "Unsupported type for Config::set");
  }
  set(path, json_value);
}

}  // namespace xtils
