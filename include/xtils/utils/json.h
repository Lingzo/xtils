#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace xtils {

class Json {
 public:
  using object_t = std::map<std::string, Json>;
  using array_t = std::vector<Json>;
  using string_t = std::string;
  using integer_t = int64_t;
  using float_t = double;
  using boolean_t = bool;
  using null_t = std::nullptr_t;

  // Define the enum for JSON types
  enum class JsonType { NUL, BOOLEAN, INTEGER, FLOAT, STRING, ARRAY, OBJECT };

  // Define the union for JSON values (using pointers for non-trivial types)
  union JsonValueUnion {
    null_t null_val;
    boolean_t bool_val;
    integer_t int_val;
    float_t float_val;
    string_t* string_val;  // Pointer to dynamically allocated string
    array_t* array_val;    // Pointer to dynamically allocated vector
    object_t* object_val;  // Pointer to dynamically allocated map

    JsonValueUnion()
        : null_val(nullptr) {
    }  // Default constructor to make it trivially constructible
    // Destructor, copy/move constructors/assignments handled by Json class.
    // They are not implicitly defined for unions with non-trivial members.
  };

  // Constructors
  Json();
  Json(std::nullptr_t);
  Json(boolean_t b);

  // Template constructor for integer types
  template <typename T>
  Json(T i, typename std::enable_if_t<
                std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0)
      : type_(JsonType::INTEGER) {
    data_.int_val = static_cast<integer_t>(i);
  }

  Json(float_t f);
  Json(const string_t& s);
  Json(const char* s);
  Json(const array_t& a);
  Json(const object_t& o);

  // Rule of Five
  ~Json();                                 // Destructor
  Json(const Json& other);                 // Copy Constructor
  Json(Json&& other) noexcept;             // Move Constructor
  Json& operator=(const Json& other);      // Copy Assignment Operator
  Json& operator=(Json&& other) noexcept;  // Move Assignment Operator

  // Type checks
  bool is_null() const { return type_ == JsonType::NUL; }
  bool is_bool() const { return type_ == JsonType::BOOLEAN; }
  bool is_integer() const { return type_ == JsonType::INTEGER; }
  bool is_float() const { return type_ == JsonType::FLOAT; }
  bool is_number() const { return is_integer() || is_float(); }
  bool is_string() const { return type_ == JsonType::STRING; }
  bool is_array() const { return type_ == JsonType::ARRAY; }
  bool is_object() const { return type_ == JsonType::OBJECT; }

  // Value access (non-const throws if type mismatch)
  boolean_t as_bool() const {
    if (!is_bool()) throw std::runtime_error("Json value is not a boolean");
    return data_.bool_val;
  }
  integer_t as_integer() const {
    if (!is_integer()) throw std::runtime_error("Json value is not an integer");
    return data_.int_val;
  }
  float_t as_float() const {
    if (!is_float()) throw std::runtime_error("Json value is not a float");
    return data_.float_val;
  }
  double as_number() const {
    if (is_integer()) return static_cast<double>(data_.int_val);
    if (is_float()) return data_.float_val;
    throw std::runtime_error("Json value is not a number");
  }
  const string_t& as_string() const {
    if (!is_string()) throw std::runtime_error("Json value is not a string");
    return *data_.string_val;
  }
  const array_t& as_array() const {
    if (!is_array()) throw std::runtime_error("Json value is not an array");
    return *data_.array_val;
  }
  const object_t& as_object() const {
    if (!is_object()) throw std::runtime_error("Json value is not an object");
    return *data_.object_val;
  }

  // Non-const operator[] - creates key/index if not exists
  Json& operator[](const std::string& key);

  // Const operator[] - throws if key doesn't exist
  const Json& operator[](const std::string& key) const;

  // Non-const operator[] - throws if index out of bounds
  Json& operator[](size_t i);

  // Const operator[] - throws if index out of bounds
  const Json& operator[](size_t i) const;

  // Safe optional access methods
  std::optional<Json> get(const std::string& key) const;
  std::optional<Json> get(size_t index) const;

  // Key/index existence checks
  bool has_key(const std::string& key) const;
  bool has_index(size_t index) const;

  // Safe typed access methods
  std::optional<boolean_t> get_bool(const std::string& key) const;
  std::optional<integer_t> get_integer(const std::string& key) const;
  std::optional<float_t> get_float(const std::string& key) const;
  std::optional<double> get_number(const std::string& key) const;
  std::optional<string_t> get_string(const std::string& key) const;
  std::optional<array_t> get_array(const std::string& key) const;
  std::optional<object_t> get_object(const std::string& key) const;

  // Array typed access methods
  std::optional<boolean_t> get_bool(size_t index) const;
  std::optional<integer_t> get_integer(size_t index) const;
  std::optional<float_t> get_float(size_t index) const;
  std::optional<double> get_number(size_t index) const;
  std::optional<string_t> get_string(size_t index) const;
  std::optional<array_t> get_array(size_t index) const;
  std::optional<object_t> get_object(size_t index) const;

  // Size information
  size_t size() const;
  bool empty() const;

  // Serialization/Deserialization
  std::string dump(int indent = 0) const;
  static Json parse(const std::string& text, std::error_code& ec);
  static std::optional<Json> parse(const std::string& text);

 private:
  JsonType type_;
  JsonValueUnion data_;

  // Helper function to destroy current data_ based on type_
  void destroy_current_data();
  // Helper function to copy data from another Json object
  void copy_from(const Json& other);
};

}  // namespace xtils
