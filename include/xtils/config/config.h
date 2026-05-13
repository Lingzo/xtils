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
  Config& Define(const std::string& name, const std::string& description,
                 const Json& default_value, bool required = false);

  // Template version for C++ native types
  template <typename T>
  Config& Define(const std::string& name, const std::string& description,
                 const T& default_value, bool required = false);

  // Loading methods
  // ParseArgs supports --config-file parameter to load configuration file
  // first, then command line arguments can override file settings
  bool ParseArgs(int argc, const char** argv, bool allow_exit = false);
  bool ParseArgs(const std::vector<std::string>& args,
                 bool allow_exit = false);
  bool LoadFile(const std::string& filename);
  bool ParseJson(const Json& json);
  bool Parse(const std::string& json_content);

  // Primary access method with dot notation support (e.g., "server.port")
  template <typename T>
  std::optional<T> Get(const std::string& path) const;

  // Specialized getters for common types
  std::optional<std::string> GetString(const std::string& path) const;
  std::optional<int64_t> GetInt(const std::string& path) const;
  std::optional<double> GetDouble(const std::string& path) const;
  std::optional<bool> GetBool(const std::string& path) const;
  std::optional<Json> Get(const std::string& path) const;

  // Utility methods
  bool Has(const std::string& path) const;
  void Set(const std::string& path, const Json& value);

  // Template version for C++ native types
  template <typename T>
  void Set(const std::string& path, const T& value);

  // Validation
  bool Validate() const;
  std::string Help() const;
  std::vector<std::string> MissingRequired() const;
  std::vector<std::string> NoParsed() const;

  // Serialization
  std::string ToString() const;
  Json ToJson() const;
  bool Save(const std::string& filename) const;
  void Print() const;
  auto Options() { return options_; }

  // Deprecated wrappers
  [[deprecated("Use Define() instead")]]
  Config& define(const std::string& name, const std::string& description,
                 const Json& default_value, bool required = false) {
    return Define(name, description, default_value, required);
  }
  template <typename T>
  [[deprecated("Use Define() instead")]]
  Config& define(const std::string& name, const std::string& description,
                 const T& default_value, bool required = false) {
    return Define(name, description, default_value, required);
  }
  [[deprecated("Use ParseArgs() instead")]]
  bool parse_args(int argc, const char** argv, bool allow_exit = false) {
    return ParseArgs(argc, argv, allow_exit);
  }
  [[deprecated("Use ParseArgs() instead")]]
  bool parse_args(const std::vector<std::string>& args,
                  bool allow_exit = false) {
    return ParseArgs(args, allow_exit);
  }
  [[deprecated("Use LoadFile() instead")]]
  bool load_file(const std::string& filename) { return LoadFile(filename); }
  [[deprecated("Use ParseJson() instead")]]
  bool parse_json(const Json& json) { return ParseJson(json); }
  [[deprecated("Use Parse() instead")]]
  bool parse(const std::string& json_content) { return Parse(json_content); }
  template <typename T>
  [[deprecated("Use Get() instead")]]
  std::optional<T> get(const std::string& path) const { return Get<T>(path); }
  [[deprecated("Use GetString() instead")]]
  std::optional<std::string> get_string(const std::string& path) const {
    return GetString(path);
  }
  [[deprecated("Use GetInt() instead")]]
  std::optional<int64_t> get_int(const std::string& path) const {
    return GetInt(path);
  }
  [[deprecated("Use GetDouble() instead")]]
  std::optional<double> get_double(const std::string& path) const {
    return GetDouble(path);
  }
  [[deprecated("Use GetBool() instead")]]
  std::optional<bool> get_bool(const std::string& path) const {
    return GetBool(path);
  }
  [[deprecated("Use Get() instead")]]
  std::optional<Json> get(const std::string& path) const { return Get(path); }
  [[deprecated("Use Has() instead")]]
  bool has(const std::string& path) const { return Has(path); }
  [[deprecated("Use Set() instead")]]
  void set(const std::string& path, const Json& value) { Set(path, value); }
  template <typename T>
  [[deprecated("Use Set() instead")]]
  void set(const std::string& path, const T& value) { Set(path, value); }
  [[deprecated("Use Validate() instead")]]
  bool validate() const { return Validate(); }
  [[deprecated("Use Help() instead")]]
  std::string help() const { return Help(); }
  [[deprecated("Use MissingRequired() instead")]]
  std::vector<std::string> missing_required() const { return MissingRequired(); }
  [[deprecated("Use NoParsed() instead")]]
  std::vector<std::string> no_parsed() const { return NoParsed(); }
  [[deprecated("Use ToString() instead")]]
  std::string to_string() const { return ToString(); }
  [[deprecated("Use ToJson() instead")]]
  Json to_json() const { return ToJson(); }
  [[deprecated("Use Save() instead")]]
  bool save(const std::string& filename) const { return Save(filename); }
  [[deprecated("Use Print() instead")]]
  void print() const { Print(); }
  [[deprecated("Use Options() instead")]]
  auto options() { return Options(); }

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

  // Private helpers for template deduplication
  template <typename T>
  static Json to_json_value(const T& value);
  template <typename T>
  static std::optional<T> from_json_value(const Json& json_val);
};

// Private helper: convert C++ value to Json
template <typename T>
Json Config::to_json_value(const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return Json(value);
  } else if constexpr (std::is_same_v<T, const char*> || std::is_array_v<T>) {
    return Json(std::string(value));
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    return Json(static_cast<int64_t>(value));
  } else if constexpr (std::is_floating_point_v<T>) {
    return Json(static_cast<double>(value));
  } else if constexpr (std::is_same_v<T, bool>) {
    return Json(value);
  } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
    Json::array_t arr;
    for (const auto& val : value) arr.push_back(Json(val));
    return Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<double>>) {
    Json::array_t arr;
    for (const auto& val : value) arr.push_back(Json(val));
    return Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    Json::array_t arr;
    for (const auto& val : value) arr.push_back(Json(val));
    return Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<int>>) {
    Json::array_t arr;
    for (const auto& val : value) arr.push_back(Json(static_cast<int64_t>(val)));
    return Json(arr);
  } else if constexpr (std::is_same_v<T, std::vector<float>>) {
    Json::array_t arr;
    for (const auto& val : value) arr.push_back(Json(static_cast<double>(val)));
    return Json(arr);
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
                  "Unsupported type for Config");
    return Json{};
  }
}

// Private helper: extract C++ value from Json
template <typename T>
std::optional<T> Config::from_json_value(const Json& json_val) {
  if constexpr (std::is_same_v<T, std::string>) {
    if (!json_val.is_string()) return std::nullopt;
    return json_val.as_string();
  } else if constexpr (std::is_same_v<T, int64_t>) {
    if (!json_val.is_integer()) return std::nullopt;
    return json_val.as_integer();
  } else if constexpr (std::is_same_v<T, double>) {
    if (json_val.is_float()) return json_val.as_float();
    if (json_val.is_integer()) return static_cast<double>(json_val.as_integer());
    return std::nullopt;
  } else if constexpr (std::is_same_v<T, bool>) {
    if (!json_val.is_bool()) return std::nullopt;
    return json_val.as_bool();
  } else if constexpr (std::is_integral_v<T>) {
    if (!json_val.is_integer()) return std::nullopt;
    return static_cast<T>(json_val.as_integer());
  } else if constexpr (std::is_floating_point_v<T>) {
    if (json_val.is_float()) return static_cast<T>(json_val.as_float());
    if (json_val.is_integer()) return static_cast<T>(json_val.as_integer());
    return std::nullopt;
  } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
    if (!json_val.is_array()) return std::nullopt;
    std::vector<int64_t> result;
    for (const auto& val : json_val.as_array()) {
      if (val.is_integer()) result.push_back(val.as_integer());
      else if (val.is_float()) result.push_back(static_cast<int64_t>(val.as_float()));
    }
    return result;
  } else if constexpr (std::is_same_v<T, std::vector<double>>) {
    if (!json_val.is_array()) return std::nullopt;
    std::vector<double> result;
    for (const auto& val : json_val.as_array()) {
      if (val.is_float()) result.push_back(val.as_float());
      else if (val.is_integer()) result.push_back(static_cast<double>(val.as_integer()));
    }
    return result;
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    if (!json_val.is_array()) return std::nullopt;
    std::vector<std::string> result;
    for (const auto& val : json_val.as_array()) {
      if (val.is_string()) result.push_back(val.as_string());
    }
    return result;
  } else if constexpr (std::is_same_v<T, std::vector<int>>) {
    if (!json_val.is_array()) return std::nullopt;
    std::vector<int> result;
    for (const auto& val : json_val.as_array()) {
      if (val.is_integer()) result.push_back(static_cast<int>(val.as_integer()));
      else if (val.is_float()) result.push_back(static_cast<int>(val.as_float()));
    }
    return result;
  } else if constexpr (std::is_same_v<T, std::vector<float>>) {
    if (!json_val.is_array()) return std::nullopt;
    std::vector<float> result;
    for (const auto& val : json_val.as_array()) {
      if (val.is_float()) result.push_back(static_cast<float>(val.as_float()));
      else if (val.is_integer()) result.push_back(static_cast<float>(val.as_integer()));
    }
    return result;
  } else if constexpr (std::is_same_v<T, Json>) {
    return json_val;
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
    return std::nullopt;
  }
}

// Template implementation
template <typename T>
std::optional<T> Config::Get(const std::string& path) const {
  auto json_val = Get(path);
  if (!json_val) return std::nullopt;
  return from_json_value<T>(*json_val);
}

// Template implementation for Define with native types
template <typename T>
Config& Config::Define(const std::string& name, const std::string& description,
                       const T& default_value, bool required) {
  return Define(name, description, to_json_value(default_value), required);
}

// Template implementation for Set with native types
template <typename T>
void Config::Set(const std::string& path, const T& value) {
  Set(path, to_json_value(value));
}

}  // namespace xtils
