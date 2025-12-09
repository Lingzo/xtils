#include "xtils/utils/json.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>  // For std::move

namespace xtils {

// Forward declaration for recursive parsing
Json parse_value(std::istream& in, int depth = 0);
void dump_value(const Json& json, std::string& out, int depth, int indent);
void dump_string(const std::string& str, std::string& out);
std::string to_string_trimmed(double value, int precision = 6) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  std::string s = out.str();
  s.erase(s.find_last_not_of('0') + 1, std::string::npos);
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

// --- Helper functions for parsing ---

static const int MAX_PARSE_DEPTH = 100;

bool check_stream_state(std::istream& in) {
  if (in.eof() || in.fail() || in.bad()) {
    return false;
  }
  return true;
}

void skip_ws(std::istream& in) {
  while (check_stream_state(in) && std::isspace(in.peek())) {
    in.get();
  }
}

std::string parse_string(std::istream& in, int depth) {
  if (depth > MAX_PARSE_DEPTH) {
    throw std::runtime_error("Maximum parsing depth exceeded");
  }

  if (!check_stream_state(in) || in.peek() != '"') {
    throw std::runtime_error("Expected '\"' at start of string");
  }

  std::string result;
  in.get();  // skip '"'

  while (check_stream_state(in) && in.peek() != '"') {
    char c = in.peek();

    // Check for unescaped control characters (allow UTF-8 high bytes)
    if ((unsigned char)c < 0x20) {
      throw std::runtime_error("Invalid control character in string");
    }

    if (in.peek() == '\\') {
      in.get();
      if (!check_stream_state(in)) {
        throw std::runtime_error("Unexpected end of input in escape sequence");
      }

      char escaped = in.peek();
      if (escaped == '"')
        result += '"';
      else if (escaped == 'n')
        result += '\n';
      else if (escaped == 't')
        result += '\t';
      else if (escaped == 'r')
        result += '\r';
      else if (escaped == '\\')
        result += '\\';
      else if (escaped == '/')
        result += '/';
      else if (escaped == 'b')
        result += '\b';
      else if (escaped == 'f')
        result += '\f';
      else if (escaped == 'u') {
        // Handle unicode escape sequence \uXXXX
        in.get();  // consume 'u'
        std::string hex;
        for (int i = 0; i < 4; i++) {
          if (!check_stream_state(in) || !std::isxdigit(in.peek())) {
            throw std::runtime_error("Invalid unicode escape sequence");
          }
          hex += in.get();
        }
        // Convert Unicode code point to UTF-8
        int code = std::stoi(hex, nullptr, 16);
        if (code <= 0x7F) {
          // 1-byte UTF-8 (ASCII)
          result += static_cast<char>(code);
        } else if (code <= 0x7FF) {
          // 2-byte UTF-8
          result += static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
          result += static_cast<char>(0x80 | (code & 0x3F));
        } else {
          // 3-byte UTF-8 (covers most Unicode including Chinese)
          result += static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
          result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
          result += static_cast<char>(0x80 | (code & 0x3F));
        }
        continue;  // Skip the normal in.get() at the end
      } else {
        throw std::runtime_error("Invalid escape sequence: \\" +
                                 std::string(1, escaped));
      }
      in.get();
    } else {
      result += in.get();
    }
  }

  if (!check_stream_state(in) || in.peek() != '"') {
    throw std::runtime_error("Unterminated string");
  }

  in.get();  // skip closing '"'
  return result;
}

Json parse_array(std::istream& in, int depth) {
  if (depth > MAX_PARSE_DEPTH) {
    throw std::runtime_error("Maximum parsing depth exceeded");
  }

  if (!check_stream_state(in) || in.peek() != '[') {
    throw std::runtime_error("Expected '[' at start of array");
  }

  Json::array_t arr;
  in.get();  // skip '['
  skip_ws(in);

  while (check_stream_state(in) && in.peek() != ']') {
    arr.push_back(parse_value(in, depth + 1));
    skip_ws(in);

    if (!check_stream_state(in)) {
      throw std::runtime_error("Unexpected end of input in array");
    }

    if (in.peek() == ',') {
      in.get();
      skip_ws(in);
      // Check for trailing comma
      if (!check_stream_state(in) || in.peek() == ']') {
        throw std::runtime_error("Trailing comma not allowed in array");
      }
    } else if (in.peek() != ']') {
      throw std::runtime_error("Expected ',' or ']' in array");
    }
  }

  if (!check_stream_state(in) || in.peek() != ']') {
    throw std::runtime_error("Unterminated array");
  }

  in.get();          // skip ']'
  return Json(arr);  // Construct Json object
}

Json parse_object(std::istream& in, int depth) {
  if (depth > MAX_PARSE_DEPTH) {
    throw std::runtime_error("Maximum parsing depth exceeded");
  }

  if (!check_stream_state(in) || in.peek() != '{') {
    throw std::runtime_error("Expected '{' at start of object");
  }

  Json::object_t obj;
  in.get();  // skip '{'
  skip_ws(in);

  while (check_stream_state(in) && in.peek() != '}') {
    if (in.peek() != '"') {
      throw std::runtime_error("Expected string key in object");
    }

    auto key = parse_string(in, depth + 1);
    skip_ws(in);

    if (!check_stream_state(in) || in.peek() != ':') {
      throw std::runtime_error("Expected ':' after object key");
    }

    in.get();  // skip ':'
    skip_ws(in);

    // Check for duplicate keys
    if (obj.find(key) != obj.end()) {
      throw std::runtime_error("Duplicate key in object: " + key);
    }
    obj[key] = parse_value(in, depth + 1);
    skip_ws(in);

    if (!check_stream_state(in)) {
      throw std::runtime_error("Unexpected end of input in object");
    }

    if (in.peek() == ',') {
      in.get();
      skip_ws(in);
      // Check for trailing comma
      if (!check_stream_state(in) || in.peek() == '}') {
        throw std::runtime_error("Trailing comma not allowed in object");
      }
    } else if (in.peek() != '}') {
      throw std::runtime_error("Expected ',' or '}' in object");
    }
  }

  if (!check_stream_state(in) || in.peek() != '}') {
    throw std::runtime_error("Unterminated object");
  }

  in.get();          // skip '}'
  return Json(obj);  // Construct Json object
}

Json parse_number(std::istream& in) {
  if (!check_stream_state(in)) {
    throw std::runtime_error("Unexpected end of input when parsing number");
  }

  std::string number_str;
  bool has_decimal = false;
  bool has_exponent = false;
  bool has_digit_before_decimal = false;

  char first_char = in.peek();

  // Handle negative sign
  if (first_char == '-') {
    number_str += in.get();
    if (!check_stream_state(in)) {
      throw std::runtime_error("Invalid number: lone minus sign");
    }
    first_char = in.peek();  // Update first_char after consuming '-'
  }

  // Handle integer part
  if (std::isdigit(first_char)) {
    has_digit_before_decimal = true;

    // Special handling for leading zero
    if (first_char == '0') {
      number_str += in.get();  // consume the '0'
      // Check if there are more digits after '0' (invalid leading zero)
      if (check_stream_state(in) && std::isdigit(in.peek()) &&
          in.peek() != '.') {
        throw std::runtime_error("Invalid number: leading zeros not allowed");
      }
    } else {
      // Normal case: consume all digits
      while (check_stream_state(in) && std::isdigit(in.peek())) {
        number_str += in.get();
      }
    }
  } else {
    // If no digit before decimal, it must be a decimal point,
    // which implies an error like ".5"
    if (first_char == '.') {
      throw std::runtime_error(
          "Invalid number: missing digits before decimal point");
    }
    throw std::runtime_error("Invalid number: expected digit or '-'");
  }

  // Handle fractional part
  if (check_stream_state(in) && in.peek() == '.') {
    number_str += in.get();
    has_decimal = true;
    // Must have at least one digit after decimal point
    if (!check_stream_state(in) || !std::isdigit(in.peek())) {
      throw std::runtime_error(
          "Invalid number: missing digits after decimal point");
    }
    while (check_stream_state(in) && std::isdigit(in.peek())) {
      number_str += in.get();
    }
  }

  // Handle exponent part
  if (check_stream_state(in) && (in.peek() == 'e' || in.peek() == 'E')) {
    number_str += in.get();
    has_exponent = true;
    // Exponent sign (optional)
    if (check_stream_state(in) && (in.peek() == '+' || in.peek() == '-')) {
      number_str += in.get();
    }
    // Must have at least one digit after exponent sign
    if (!check_stream_state(in) || !std::isdigit(in.peek())) {
      throw std::runtime_error("Invalid number: missing digits after exponent");
    }
    while (check_stream_state(in) && std::isdigit(in.peek())) {
      number_str += in.get();
    }
  }

  if (has_decimal || has_exponent) {
    return Json(std::stod(number_str));
  } else {
    return Json(std::stoll(number_str));
  }
}

Json parse_literal(std::istream& in, const std::string& literal,
                   const Json& value) {
  for (char c : literal) {
    if (!check_stream_state(in) || in.peek() != c) {
      throw std::runtime_error("Invalid literal: expected '" + literal + "'");
    }
    in.get();
  }
  return value;
}

Json parse_value(std::istream& in, int depth) {
  if (depth > MAX_PARSE_DEPTH) {
    throw std::runtime_error("Maximum parsing depth exceeded");
  }

  skip_ws(in);

  if (!check_stream_state(in)) {
    throw std::runtime_error("Unexpected end of input");
  }

  char c = in.peek();

  if (c == '"') {
    return Json(parse_string(in, depth));
  } else if (c == '{') {
    return parse_object(in, depth);
  } else if (c == '[') {
    return parse_array(in, depth);
  } else if (std::isdigit(c) || c == '-') {
    return parse_number(in);
  } else if (c == 't') {
    return parse_literal(in, "true", true);
  } else if (c == 'f') {
    return parse_literal(in, "false", false);
  } else if (c == 'n') {
    return parse_literal(in, "null", nullptr);
  } else {
    throw std::runtime_error(
        "Invalid JSON value at position " +
        std::to_string(static_cast<long long>(in.tellg())) + ", character '" +
        std::string(1, c) + "'");
  }
}

// --- Json Class Implementations ---

// Helper to destroy current data based on type
void Json::destroy_current_data() {
  switch (type_) {
    case JsonType::STRING:
      delete data_.string_val;
      break;
    case JsonType::ARRAY:
      delete data_.array_val;
      break;
    case JsonType::OBJECT:
      delete data_.object_val;
      break;
    default:
      // For NUL, BOOLEAN, INTEGER, FLOAT, no dynamic memory to free
      break;
  }
}

// Helper to copy data from another Json object
void Json::copy_from(const Json& other) {
  type_ = other.type_;
  switch (type_) {
    case JsonType::NUL:
      data_.null_val = nullptr;
      break;
    case JsonType::BOOLEAN:
      data_.bool_val = other.data_.bool_val;
      break;
    case JsonType::INTEGER:
      data_.int_val = other.data_.int_val;
      break;
    case JsonType::FLOAT:
      data_.float_val = other.data_.float_val;
      break;
    case JsonType::STRING:
      data_.string_val = new string_t(*other.data_.string_val);
      break;
    case JsonType::ARRAY:
      data_.array_val = new array_t(*other.data_.array_val);
      break;
    case JsonType::OBJECT:
      data_.object_val = new object_t(*other.data_.object_val);
      break;
  }
}

// Default Constructor
Json::Json() : type_(JsonType::NUL) { data_.null_val = nullptr; }

// Null Constructor
Json::Json(std::nullptr_t) : type_(JsonType::NUL) { data_.null_val = nullptr; }

// Boolean Constructor
Json::Json(boolean_t b) : type_(JsonType::BOOLEAN) { data_.bool_val = b; }

// Float Constructor (for float)
Json::Json(float_t f) : type_(JsonType::FLOAT) { data_.float_val = f; }

// String Constructor (from std::string)
Json::Json(const string_t& s) : type_(JsonType::STRING) {
  data_.string_val = new string_t(s);
}

// String Constructor (from const char*)
Json::Json(const char* s) : type_(JsonType::STRING) {
  data_.string_val = new string_t(s);
}

// Array Constructor
Json::Json(const array_t& a) : type_(JsonType::ARRAY) {
  data_.array_val = new array_t(a);
}

// Object Constructor
Json::Json(const object_t& o) : type_(JsonType::OBJECT) {
  data_.object_val = new object_t(o);
}

// Destructor
Json::~Json() { destroy_current_data(); }

// Copy Constructor
Json::Json(const Json& other) { copy_from(other); }

// Move Constructor
Json::Json(Json&& other) noexcept : type_(other.type_), data_(other.data_) {
  // Reset other to a null state to prevent double-free
  other.type_ = JsonType::NUL;
  other.data_.null_val = nullptr;
}

// Copy Assignment Operator
Json& Json::operator=(const Json& other) {
  if (this != &other) {  // Handle self-assignment
    destroy_current_data();
    copy_from(other);
  }
  return *this;
}

// Move Assignment Operator
Json& Json::operator=(Json&& other) noexcept {
  if (this != &other) {      // Handle self-assignment
    destroy_current_data();  // Clean up existing resources

    // Steal resources from other
    type_ = other.type_;
    data_ = other.data_;

    // Reset other to a null state
    other.type_ = JsonType::NUL;
    other.data_.null_val = nullptr;
  }
  return *this;
}

// Non-const operator[] for object access
Json& Json::operator[](const std::string& key) {
  if (!is_object()) {
    destroy_current_data();  // Clean up old data if type changes
    type_ = JsonType::OBJECT;
    data_.object_val = new object_t{};
  }
  return (*data_.object_val)[key];
}

// Const operator[] for object access
const Json& Json::operator[](const std::string& key) const {
  if (!is_object()) {
    throw std::runtime_error("Json value is not an object");
  }
  const auto& obj = *data_.object_val;
  auto it = obj.find(key);
  if (it == obj.end()) {
    throw std::runtime_error("Key '" + key + "' not found in Json object");
  }
  return it->second;
}

// Non-const operator[] for array access
Json& Json::operator[](size_t i) {
  if (!is_array()) {
    // Original behavior throws if not array. We adhere to that.
    throw std::runtime_error("Json value is not an array");
  }
  auto& arr = *data_.array_val;
  if (i >= arr.size()) {
    throw std::runtime_error(
        "Array index " + std::to_string(i) +
        " out of bounds (size: " + std::to_string(arr.size()) + ")");
  }
  return arr[i];
}

// Const operator[] for array access
const Json& Json::operator[](size_t i) const {
  if (!is_array()) {
    throw std::runtime_error("Json value is not an array");
  }
  const auto& arr = *data_.array_val;
  if (i >= arr.size()) {
    throw std::runtime_error(
        "Array index " + std::to_string(i) +
        " out of bounds (size: " + std::to_string(arr.size()) + ")");
  }
  return arr[i];
}

// Push back method for arrays
Json& Json::push_back(const Json& value) {
  if (!is_array()) {
    destroy_current_data();  // Clean up old data if type changes
    type_ = JsonType::ARRAY;
    data_.array_val = new array_t{};
  }
  data_.array_val->push_back(value);
  return *this;
}

void Json::clear() noexcept {
  destroy_current_data();
  type_ = JsonType::NUL;
  data_.null_val = nullptr;
}

void Json::erase(const std::string& key) {
  if (!is_object()) {
    throw std::runtime_error("Json value is not an object");
  }
  data_.object_val->erase(key);
}

void Json::erase(size_t index) {
  if (!is_array()) {
    throw std::runtime_error("Json value is not an array");
  }
  auto& arr = *data_.array_val;
  if (index >= arr.size()) {
    throw std::runtime_error(
        "Array index " + std::to_string(index) +
        " out of bounds (size: " + std::to_string(arr.size()) + ")");
  }
  arr.erase(arr.begin() + index);
}

// Safe optional access methods (key)
std::optional<Json> Json::get(const std::string& key) const {
  if (!is_object()) return std::nullopt;
  const auto& obj = *data_.object_val;
  auto it = obj.find(key);
  return it != obj.end() ? std::optional<Json>(it->second) : std::nullopt;
}

// Safe optional access methods (index)
std::optional<Json> Json::get(size_t index) const {
  if (!is_array()) return std::nullopt;
  const auto& arr = *data_.array_val;
  return index < arr.size() ? std::optional<Json>(arr[index]) : std::nullopt;
}

// Key existence checks
bool Json::has_key(const std::string& key) const {
  if (!is_object()) return false;
  const auto& obj = *data_.object_val;
  return obj.find(key) != obj.end();
}

// Index existence checks
bool Json::has_index(size_t index) const {
  if (!is_array()) return false;
  const auto& arr = *data_.array_val;
  return index < arr.size();
}

// Safe typed access methods (key)
std::optional<Json::boolean_t> Json::get_bool(const std::string& key) const {
  auto val = get(key);
  return val && val->is_bool() ? std::optional<boolean_t>(val->as_bool())
                               : std::nullopt;
}

std::optional<Json::integer_t> Json::get_integer(const std::string& key) const {
  auto val = get(key);
  return val && val->is_integer() ? std::optional<integer_t>(val->as_integer())
                                  : std::nullopt;
}

std::optional<Json::float_t> Json::get_float(const std::string& key) const {
  auto val = get(key);
  return val && val->is_float() ? std::optional<float_t>(val->as_float())
                                : std::nullopt;
}

std::optional<double> Json::get_number(const std::string& key) const {
  auto val = get(key);
  return val && val->is_number() ? std::optional<double>(val->as_number())
                                 : std::nullopt;
}

std::optional<Json::string_t> Json::get_string(const std::string& key) const {
  auto val = get(key);
  return val && val->is_string() ? std::optional<string_t>(val->as_string())
                                 : std::nullopt;
}

std::optional<Json::array_t> Json::get_array(const std::string& key) const {
  auto val = get(key);
  return val && val->is_array() ? std::optional<array_t>(val->as_array())
                                : std::nullopt;
}

std::optional<Json::object_t> Json::get_object(const std::string& key) const {
  auto val = get(key);
  return val && val->is_object() ? std::optional<object_t>(val->as_object())
                                 : std::nullopt;
}

// Array typed access methods (index)
std::optional<Json::boolean_t> Json::get_bool(size_t index) const {
  auto val = get(index);
  return val && val->is_bool() ? std::optional<boolean_t>(val->as_bool())
                               : std::nullopt;
}

std::optional<Json::integer_t> Json::get_integer(size_t index) const {
  auto val = get(index);
  return val && val->is_integer() ? std::optional<integer_t>(val->as_integer())
                                  : std::nullopt;
}

std::optional<Json::float_t> Json::get_float(size_t index) const {
  auto val = get(index);
  return val && val->is_float() ? std::optional<float_t>(val->as_float())
                                : std::nullopt;
}

std::optional<double> Json::get_number(size_t index) const {
  auto val = get(index);
  return val && val->is_number() ? std::optional<double>(val->as_number())
                                 : std::nullopt;
}

std::optional<Json::string_t> Json::get_string(size_t index) const {
  auto val = get(index);
  return val && val->is_string() ? std::optional<string_t>(val->as_string())
                                 : std::nullopt;
}

std::optional<Json::array_t> Json::get_array(size_t index) const {
  auto val = get(index);
  return val && val->is_array() ? std::optional<array_t>(val->as_array())
                                : std::nullopt;
}

std::optional<Json::object_t> Json::get_object(size_t index) const {
  auto val = get(index);
  return val && val->is_object() ? std::optional<object_t>(val->as_object())
                                 : std::nullopt;
}

// Size information
size_t Json::size() const {
  if (is_array()) return data_.array_val->size();
  if (is_object()) return data_.object_val->size();
  return 0;
}

bool Json::empty() const {
  if (is_array()) return data_.array_val->empty();
  if (is_object()) return data_.object_val->empty();
  if (is_string()) return data_.string_val->empty();
  return is_null();
}

void dump_string(const std::string& str, std::string& out) {
  out.push_back('"');
  for (unsigned char c : str) {
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      case '\b':
        out.append("\\b");
        break;
      case '\f':
        out.append("\\f");
        break;
      default:
        if (c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out.append(buf);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
}

void dump_value(const Json& json, std::string& out, int depth, int indent) {
  const std::string indent_str(indent * depth, ' ');
  const std::string new_line = indent > 0 ? "\n" : "";
  const std::string comma = indent > 0 ? ",\n" : ",";
  const std::string colon = indent > 0 ? ": " : ":";
  if (json.is_null()) {
    out.append("null");
  } else if (json.is_bool()) {
    out.append(json.as_bool() ? "true" : "false");
  } else if (json.is_integer()) {
    out.append(std::to_string(json.as_integer()));
  } else if (json.is_float()) {
    out.append(to_string_trimmed(json.as_float()));
  } else if (json.is_string()) {
    dump_string(json.as_string(), out);
  } else if (json.is_array()) {
    const auto& arr = json.as_array();
    out.append("[");
    if (!arr.empty()) {
      out.append(new_line);
      bool first = true;
      for (const auto& val : arr) {
        if (!first) {
          out.append(comma);
        }
        out.append(indent_str);
        dump_value(val, out, depth + 1, indent);
        first = false;
      }
      out.append(new_line);
      out.append(indent_str.substr(0, indent_str.size() - indent));
    }
    out.append("]");
  } else if (json.is_object()) {
    const auto& obj = json.as_object();
    out.append("{");
    if (!obj.empty()) {
      out.append(new_line);
      bool first = true;
      for (const auto& [k, v] : obj) {
        if (!first) {
          out.append(comma);
        }
        out.append(indent_str);
        dump_string(k, out);
        out.append(colon);
        dump_value(v, out, depth + 1, indent);
        first = false;
      }
      out.append(new_line);
      out.append(indent_str.substr(0, indent_str.size() - indent));
    }
    out.append("}");
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  out.reserve(1024);  // Pre-allocate some space
  dump_value(*this, out, 1, indent);
  return out;
}

std::optional<Json> Json::parse(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }

  std::istringstream ss(text);
  try {
    auto result = parse_value(ss, 0);

    // Check if there are unparsed non-whitespace characters
    skip_ws(ss);
    if (ss.peek() != EOF) {
      return std::nullopt;  // Extra characters found
    }

    return result;
  } catch (const std::exception& e) {
    // Catch parsing errors and return nullopt
    return std::nullopt;
  }
}

Json Json::parse(const std::string& text, std::error_code& ec) {
  if (text.empty()) {
    ec.assign(static_cast<int>(std::errc::invalid_argument),
              std::system_category());
    return Json();
  }

  std::istringstream ss(text);
  try {
    auto result = parse_value(ss, 0);

    // Check if there are unparsed non-whitespace characters
    skip_ws(ss);
    if (ss.peek() != EOF) {
      ec.assign(static_cast<int>(std::errc::invalid_argument),
                std::system_category());
      return Json();
    }

    ec.clear();
    return result;
  } catch (const std::exception& e) {
    ec.assign(static_cast<int>(std::errc::invalid_argument),
              std::system_category());
    return Json();
  }
}

bool Json::contains(const std::string& key) const { return has_key(key); }
}  // namespace xtils
