#include "xtils/config/config.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace xtils {

// Helper function for C++17 compatibility (starts_with is C++20)
static bool starts_with(const std::string& str, const std::string& prefix) {
  return str.length() >= prefix.length() &&
         str.substr(0, prefix.length()) == prefix;
}

Config& Config::define(const std::string& name, const std::string& description,
                       const Json& default_value, bool required) {
  options_[name] = Option(name, description, default_value, required);

  // Apply default value immediately if not already set
  if (!has(name)) {
    set(name, default_value);
  }

  return *this;
}

bool Config::parse_args(int argc, char* argv[]) {
  // First, apply default values
  apply_defaults();

  // First pass: look for --config-file parameter
  std::string config_file;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--config-file" && i + 1 < argc) {
      config_file = argv[i + 1];
      break;
    } else if (starts_with(arg, "--config-file=")) {
      config_file = arg.substr(14);  // length of "--config-file="
      break;
    }
  }

  // Load config file if specified
  if (!config_file.empty()) {
    if (!load_file(config_file)) {
      std::cerr << "Failed to load config file: " << config_file << std::endl;
      return false;
    }
  }

  // Second pass: process all command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    // Skip help flags
    if (arg == "-h" || arg == "--help") {
      continue;
    }

    // Skip config-file argument as it's already processed
    if (arg == "--config-file") {
      ++i;  // Skip the next argument (the file path)
      continue;
    }
    if (starts_with(arg, "--config-file=")) {
      continue;
    }

    std::string key;
    std::string value_str;
    bool has_value = false;

    if (arg.length() >= 2 && arg.substr(0, 2) == "--") {
      // Long form: --key=value or --key value
      std::string long_arg = arg.substr(2);
      size_t eq_pos = long_arg.find('=');

      if (eq_pos != std::string::npos) {
        key = long_arg.substr(0, eq_pos);
        value_str = long_arg.substr(eq_pos + 1);
        has_value = true;
      } else {
        key = long_arg;
        // Check if next argument is the value
        if (i + 1 < argc && !starts_with(std::string(argv[i + 1]), "-")) {
          value_str = argv[++i];
          has_value = true;
        }
      }
    }

    if (key.empty()) {
      std::cerr << "Unknown argument: " << arg << std::endl;
      return false;
    }

    auto option_it = options_.find(key);
    if (option_it == options_.end()) {
      std::cerr << "Unknown option: " << key << std::endl;
      return false;
    }

    // For boolean options, if no value is provided, treat as true
    if (!has_value && option_it->second.default_value.is_bool()) {
      set(key, Json(true));
    } else if (!has_value) {
      std::cerr << "Option " << key << " requires a value" << std::endl;
      return false;
    } else {
      auto parsed_value =
          parse_value(value_str, option_it->second.default_value);
      if (!parsed_value) {
        std::cerr << "Invalid value for option " << key << ": " << value_str
                  << std::endl;
        return false;
      }
      set(key, parsed_value.value());
    }
  }

  return validate();
}

bool Config::load_file(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Failed to open config file: " << filename << std::endl;
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();

  return parse_json(buffer.str());
}

bool Config::parse_json(const std::string& json_content) {
  auto json = Json::parse(json_content);
  if (!json) {
    std::cerr << "Failed to parse JSON configuration" << std::endl;
    return false;
  }

  // Apply defaults first
  apply_defaults();

  // Merge JSON values
  if (json->is_object()) {
    data_ = merge_objects(data_, *json);
  }

  return validate();
}

std::string Config::get_string(const std::string& path,
                               const std::string& default_value) const {
  return get<std::string>(path, default_value);
}

int64_t Config::get_int(const std::string& path, int64_t default_value) const {
  return get<int64_t>(path, default_value);
}

double Config::get_double(const std::string& path, double default_value) const {
  return get<double>(path, default_value);
}

bool Config::get_bool(const std::string& path, bool default_value) const {
  return get<bool>(path, default_value);
}

bool Config::has(const std::string& path) const {
  return get(path).has_value();
}

void Config::set(const std::string& path, const Json& value) {
  auto parts = split_path(path);
  if (parts.empty()) return;

  // Ensure data_ is an object
  if (!data_.is_object()) {
    data_ = Json::object_t{};
  }

  Json* current = &data_;

  // Navigate to the parent of the target key
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    if (!current->is_object()) {
      *current = Json::object_t{};
    }

    // Get or create the nested object
    if (!current->has_key(parts[i])) {
      (*current)[parts[i]] = Json::object_t{};
    }
    current = &(*current)[parts[i]];
  }

  // Ensure the current level is an object
  if (!current->is_object()) {
    *current = Json::object_t{};
  }

  // Set the final value
  (*current)[parts.back()] = value;
}

bool Config::validate() const {
  auto missing = missing_required();
  if (!missing.empty()) {
    std::cerr << "Missing required options: ";
    for (size_t i = 0; i < missing.size(); ++i) {
      std::cerr << missing[i];
      if (i < missing.size() - 1) std::cerr << ", ";
    }
    std::cerr << std::endl;
    return false;
  }
  return true;
}

std::string Config::help() const {
  std::ostringstream oss;
  oss << "Configuration Options:\n";
  oss << "  --config-file <file>\n";
  oss << "      Load configuration from JSON file. Command line arguments\n";
  oss << "      can override values from the config file.\n";
  oss << "\n";

  for (const auto& [name, option] : options_) {
    oss << "  --" << option.name;

    if (option.required) {
      oss << " (required)";
    } else {
      oss << " (default: ";
      if (option.default_value.is_string()) {
        oss << option.default_value.as_string();
      } else {
        oss << option.default_value.dump();
      }
      oss << ")";
    }

    oss << "\n      " << option.description << "\n";
  }

  return oss.str();
}

std::vector<std::string> Config::missing_required() const {
  std::vector<std::string> missing;

  for (const auto& [name, option] : options_) {
    if (option.required && !has(name)) {
      missing.push_back(name);
    }
  }

  return missing;
}

void Config::print() const {
  std::cout << "Current Configuration:\n";
  std::cout << data_.dump(2) << std::endl;
}

Json Config::to_json() const { return data_; }

bool Config::save(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Failed to create config file: " << filename << std::endl;
    return false;
  }

  file << to_string();
  file.close();
  return true;
}

std::string Config::to_string() const { return data_.dump(2); }

std::optional<Json> Config::parse_value(const std::string& value_str,
                                        const Json& default_value) const {
  if (default_value.is_string()) {
    return Json(value_str);
  } else if (default_value.is_integer()) {
    try {
      return Json(std::stoll(value_str));
    } catch (...) {
      return std::nullopt;
    }
  } else if (default_value.is_float()) {
    try {
      return Json(std::stod(value_str));
    } catch (...) {
      return std::nullopt;
    }
  } else if (default_value.is_bool()) {
    std::string lower = value_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
      return Json(true);
    } else if (lower == "false" || lower == "0" || lower == "no" ||
               lower == "off") {
      return Json(false);
    } else {
      return std::nullopt;
    }
  }

  return std::nullopt;
}

void Config::apply_defaults() {
  for (const auto& [name, option] : options_) {
    if (!has(name)) {
      set(name, option.default_value);
    }
  }
}

std::vector<std::string> Config::split_path(const std::string& path) const {
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string part;

  while (std::getline(ss, part, '.')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }

  return parts;
}

std::optional<Json> Config::get(const std::string& path) const {
  auto parts = split_path(path);
  if (parts.empty()) return std::nullopt;

  const Json* current = &data_;

  for (const auto& part : parts) {
    if (!current->is_object()) {
      return std::nullopt;
    }

    if (!current->has_key(part)) {
      return std::nullopt;
    }

    current = &(*current)[part];
  }

  return *current;
}

Json Config::merge_objects(const Json& xtils, const Json& overlay) const {
  if (!xtils.is_object() || !overlay.is_object()) {
    return overlay;
  }

  Json::object_t result = xtils.as_object();
  const auto& overlay_obj = overlay.as_object();

  for (const auto& [key, value] : overlay_obj) {
    if (result.find(key) != result.end() && result.at(key).is_object() &&
        value.is_object()) {
      result[key] = merge_objects(result.at(key), value);
    } else {
      result[key] = value;
    }
  }

  return Json(result);
}

}  // namespace xtils
