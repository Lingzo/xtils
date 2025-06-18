#include "json.h"

#include <iomanip>
#include <stdexcept>
#include <system_error>

namespace base {

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

std::string parse_string(std::istream& in, int depth = 0) {
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

Json parse_value(std::istream& in, int depth = 0);

Json parse_array(std::istream& in, int depth = 0) {
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

  in.get();  // skip ']'
  return arr;
}

Json parse_object(std::istream& in, int depth = 0) {
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

  in.get();  // skip '}'
  return obj;
}

Json parse_number(std::istream& in) {
  if (!check_stream_state(in)) {
    throw std::runtime_error("Unexpected end of input when parsing number");
  }

  std::string number;
  bool has_decimal = false;
  bool has_exponent = false;
  bool has_digit_before_decimal = false;
  bool has_digit_after_decimal = false;
  bool has_digit_after_exponent = false;
  bool is_negative = false;

  // 处理负号
  if (in.peek() == '-') {
    number += in.get();
    is_negative = true;
    if (!check_stream_state(in)) {
      throw std::runtime_error("Invalid number: lone minus sign");
    }
  }

  // 解析整数部分
  if (check_stream_state(in) && std::isdigit(in.peek())) {
    char first_digit = in.get();
    number += first_digit;
    has_digit_before_decimal = true;

    // 检查前导零：如果第一个数字是0，后面不能再有数字（除非是小数点或指数）
    if (first_digit == '0' && check_stream_state(in) &&
        std::isdigit(in.peek())) {
      throw std::runtime_error("Invalid number: leading zeros not allowed");
    }

    // 继续解析剩余数字
    while (check_stream_state(in) && std::isdigit(in.peek())) {
      number += in.get();
    }
  }

  // 解析小数部分
  if (check_stream_state(in) && in.peek() == '.') {
    if (!has_digit_before_decimal) {
      throw std::runtime_error(
          "Invalid number: missing digits before decimal point");
    }
    number += in.get();
    has_decimal = true;

    // 小数点后必须有数字
    if (!check_stream_state(in) || !std::isdigit(in.peek())) {
      throw std::runtime_error(
          "Invalid number: missing digits after decimal point");
    }

    while (check_stream_state(in) && std::isdigit(in.peek())) {
      number += in.get();
      has_digit_after_decimal = true;
    }
  }

  // 解析指数部分
  if (check_stream_state(in) && (in.peek() == 'e' || in.peek() == 'E')) {
    if (!has_digit_before_decimal && !has_digit_after_decimal) {
      throw std::runtime_error(
          "Invalid number: missing digits before exponent");
    }
    number += in.get();
    has_exponent = true;

    // 指数可以有正负号
    if (check_stream_state(in) && (in.peek() == '+' || in.peek() == '-')) {
      number += in.get();
    }

    // 指数后必须有数字
    if (!check_stream_state(in) || !std::isdigit(in.peek())) {
      throw std::runtime_error("Invalid number: missing digits after exponent");
    }

    while (check_stream_state(in) && std::isdigit(in.peek())) {
      number += in.get();
      has_digit_after_exponent = true;
    }
  }

  if (!has_digit_before_decimal && !has_digit_after_decimal) {
    throw std::runtime_error("Invalid number: no digits found");
  }

  try {
    // 如果有小数点或指数，解析为浮点数
    if (has_decimal || has_exponent) {
      return std::stod(number);
    } else {
      // 否则解析为整数
      int64_t int_value = std::stoll(number);
      return int_value;
    }
  } catch (const std::out_of_range& e) {
    throw std::runtime_error("Number out of range: " + number);
  } catch (const std::exception& e) {
    throw std::runtime_error("Invalid number format: " + number);
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
    return parse_string(in, depth);
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
    throw std::runtime_error("Invalid JSON value at position " +
                             std::to_string(in.tellg()) + ", character '" +
                             std::string(1, c) + "'");
  }
}

std::optional<Json> Json::parse(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }

  std::istringstream ss(text);
  try {
    auto result = parse_value(ss, 0);

    // 检查是否还有未解析的非空白字符
    skip_ws(ss);
    if (ss.peek() != EOF) {
      return std::nullopt;  // 有多余的字符
    }

    // Allow all valid JSON values as root values
    // (objects, arrays, strings, numbers, booleans, null)

    return result;
  } catch (const std::exception& e) {
    return std::nullopt;
  }
}

Json Json::parse(const std::string& text, std::error_code& ec) {
  if (text.empty()) {
    ec.assign(1, std::system_category());
    return Json();
  }

  std::istringstream ss(text);
  try {
    auto result = parse_value(ss, 0);

    // 检查是否还有未解析的非空白字符
    skip_ws(ss);
    if (ss.peek() != EOF) {
      ec.assign(1, std::system_category());
      return Json();
    }

    // Allow all valid JSON values as root values
    // (objects, arrays, strings, numbers, booleans, null)

    ec.clear();
    return result;
  } catch (const std::exception& e) {
    ec.assign(1, std::system_category());
    return Json();
  }
}

std::string indent_str(int indent) { return std::string(indent, ' '); }

std::string Json::dump(int indent) const {
  std::ostringstream out;
  if (is_null())
    out << "null";
  else if (is_bool())
    out << (as_bool() ? "true" : "false");
  else if (is_integer())
    out << as_integer();
  else if (is_float())
    out << as_float();
  else if (is_string()) {
    out << "\"";
    // 转义特殊字符
    const std::string& str = as_string();
    for (size_t i = 0; i < str.length(); ++i) {
      unsigned char c = str[i];
      switch (c) {
        case '"':
          out << "\\\"";
          break;
        case '\\':
          out << "\\\\";
          break;
        case '\n':
          out << "\\n";
          break;
        case '\r':
          out << "\\r";
          break;
        case '\t':
          out << "\\t";
          break;
        case '\b':
          out << "\\b";
          break;
        case '\f':
          out << "\\f";
          break;
        default:
          if (c < 0x20) {
            // Control characters
            out << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                << (int)c;
          } else if (c < 0x80) {
            // ASCII characters
            out << c;
          } else {
            // UTF-8 encoded characters - keep as-is for readability
            out << c;
          }
          break;
      }
    }
    out << "\"";
  } else if (is_array()) {
    out << "[";
    const auto& arr = as_array();
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0) out << ", ";
      out << arr[i].dump(indent);
    }
    out << "]";
  } else if (is_object()) {
    out << "{";
    const auto& obj = as_object();
    bool first = true;
    for (const auto& [k, v] : obj) {
      if (!first) out << ", ";
      out << "\"" << k << "\": " << v.dump(indent);
      first = false;
    }
    out << "}";
  }
  return out.str();
}

}  // namespace base
