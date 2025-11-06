#pragma once
#include <xtils/utils/json.h>

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xtils {

using Json = xtils::Json;
class AnyMap {
 public:
  template <typename T>
  void set(const std::string& name, const T& t) {
    map_[name] = t;
  }

  template <typename T>
  std::optional<T> get(const std::string& name) {
    try {
      auto e = map_[name];
      return std::any_cast<T>(e);
    } catch (const std::exception& e) {
      return std::nullopt;
    }
  }

 private:
  std::map<std::string, std::any> map_;
};

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
inline std::string demangle(const char* mangled_name) {
  int status = 0;
  char* demangled =
      abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status);
  std::string result(status == 0 ? demangled : mangled_name);
  free(demangled);
  return result;
}
#define GET_TYPE_NAME(type) demangle(typeid(type).name())
#elif defined(_MSC_VER)
#define GET_TYPE_NAME(type) typeid(type).name()
#else
#define GET_TYPE_NAME(type) typeid(type).name()
#endif
template <typename T>
std::string type_name() {
  return GET_TYPE_NAME(T);
}

template <typename T>
std::string type_name(const T&) {
  return type_name<T>();
}

struct IPort {
 public:
  IPort(const std::string& name, int mode, const std::string& type_name)
      : name(name), mode(mode), type_name(type_name) {}
  const std::string name;
  int mode;
  const std::string type_name;
};

template <typename T>
class InputPort : public IPort {
 public:
  InputPort(const std::string& name)
      : IPort(name, 0, ::xtils::type_name<T>()) {}
};

template <typename T>
class OutputPort : public IPort {
 public:
  OutputPort(const std::string& name)
      : IPort(name, 1, ::xtils::type_name<T>()) {}
};

enum class Status { Success = 0, Failure = 1, Running = 2, Idle = 3 };
enum class Type { Composite = 0, Action = 1, Decorator = 2 };
class BtTree;
class BtFactory;
class Node {
 public:
  using Ptr = std::shared_ptr<Node>;
  Node(const std::string& name, Type type) : name_(name), type_(type) {}
  virtual ~Node() = default;
  Status tick();
  virtual Status OnTick() = 0;
  virtual Status OnStart() { return Status::Running; }
  virtual void OnStop() {}
  virtual void reset();
  Type getType() const { return type_; }
  Status getStatus() const { return status_; }

  static std::vector<IPort> get_ports() { return {}; }

  template <typename T>
  std::optional<T> getInput(const std::string& name) {
    return data_.get<T>(name);
  }

  template <typename T>
  void setOutput(const std::string& name, const T& t) {
    return data_.set(name, t);
  }

 protected:
  friend class BtTree;
  friend class BtFactory;
  Type type_;
  std::string name_;
  bool started_{false};
  Status status_;
  int id_;
  std::vector<Node::Ptr> children;
  AnyMap data_;
};

// Composite nodes
class Composite : public Node {
 public:
  Composite(const std::string& name = "");
  void addChild(const Node::Ptr& c) { children.push_back(c); }
  void reset() override final;

 protected:
  size_t current = 0;
};

class Sequence : public Composite {
 public:
  Sequence(const std::string& name = "") : Composite(name) {}
  Status OnTick() override;
};

class Selector : public Composite {
 public:
  Selector(const std::string& name = "") : Composite(name) {}
  Status OnTick() override;
};

// Decorator base
class Decorator : public Node {
 public:
  Decorator(const std::string& name = "");
  void setChild(const Node::Ptr& c) {
    children.resize(1);
    children[0] = c;
  }
  void reset() override final;
};

class Inverter : public Decorator {
 public:
  Inverter(const std::string& name = "");
  Status OnTick() override;
};

// Leaves
class ActionNode : public Node {
 public:
  ActionNode(const std::string& name = "");
  virtual Status OnTick() override = 0;
  void reset() override final;
};

class SimpleAction : public ActionNode {
 public:
  SimpleAction(std::function<Status()> func, const std::string& name = "")
      : ActionNode(name), func_(func) {}
  Status OnTick() override { return func_(); }

 private:
  std::function<Status()> func_;
};

class AlwaysSuccess : public ActionNode {
 public:
  AlwaysSuccess(const std::string& name = "") : ActionNode(name) {}
  Status OnTick() override { return Status::Success; }
};

class AlwaysFailure : public ActionNode {
 public:
  AlwaysFailure(const std::string& name = "") : ActionNode(name) {}
  Status OnTick() override { return Status::Failure; }
};

class Delay : public Decorator {
 public:
  Delay(const std::string& name = "");
  Status OnTick() override;
  Status OnStart() override;
  static std::vector<IPort> get_ports() { return {InputPort<int>("delay")}; }

 private:
  int delay_;
};

class BtTree {
 public:
  using Ptr = std::shared_ptr<BtTree>;
  BtTree(Node::Ptr root, const std::string& name = "");
  Status tick();
  void reset();
  void shutdown();
  std::string dump();

 private:
  void set_node_id(Node::Ptr node);
  std::string dump_node(Node::Ptr node);
  std::atomic_int ids_{0};
  Node::Ptr root_;
  std::string name_;
  std::vector<Node::Ptr> nodes_;
};

// Factory / builder that parses JSON and constructs nodes.
class BtFactory {
 public:
  explicit BtFactory();
  // Register runtime functions
  template <typename T>
  void Register(const std::string& name) {
    Type type = Type::Composite;
    if (std::is_base_of_v<Composite, T>) {
      type = Type::Composite;
    } else if (std::is_base_of_v<Decorator, T>) {
      type = Type::Decorator;
    } else if (std::is_base_of_v<ActionNode, T>) {
      type = Type::Action;
    }
    nodes_[name] = {[](const std::string& n) { return std::make_shared<T>(n); },
                    type, T::get_ports()};
  }
  void RegisterSimpleAction(std::function<Status()> func,
                            const std::string& name) {
    nodes_[name] = {[func](const std::string& n) {
                      return std::make_shared<SimpleAction>(func, n);
                    },
                    Type::Action};
  }
  // Build tree from json
  BtTree::Ptr buildFromJson(const Json& j);
  std::string dump() const;

 private:
  struct Factory {
    std::function<Node::Ptr(const std::string&)> create;
    Type type;
    std::vector<IPort> ports;
  };
  std::unordered_map<std::string, Factory> nodes_;
  Node::Ptr buildNode(const Json& j);
};

}  // namespace xtils
