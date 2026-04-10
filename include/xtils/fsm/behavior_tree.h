#pragma once
#include <xtils/utils/json.h>
#include <xtils/utils/type_traits.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace xtils {

using Json = xtils::Json;

// Event type identifier for behavior tree event system
using EventType = int32_t;
struct AnyData {
  std::string_view type_name;
  std::shared_ptr<void> data;
  AnyData() = default;
  template <typename T>
  T* get() const {
    if (type_name != ::xtils::type_name<T>()) {
      return nullptr;
    }
    return static_cast<T*>(data.get());
  }
};

template <typename T>
AnyData make_any_data(const T& t) {
  AnyData any_data;
  any_data.type_name = ::xtils::type_name<std::decay_t<T>>();
  any_data.data = std::make_unique<std::decay_t<T>>(t);
  return any_data;
}

// Event structure for inter-node communication and tree control
struct BtEvent {
  EventType type{0};
  AnyData data;
  uint64_t timestamp{0};
};

class AnyMap {
 public:
  // no copy/move
  AnyMap(const AnyMap&) = delete;
  AnyMap& operator=(const AnyMap&) = delete;
  AnyMap() = default;

  template <typename T>
  void set(const std::string& name, const T& t) {
    map_[name] = make_any_data(t);
  }

  template <typename T>
  std::optional<T> get(const std::string& name) const {
    auto it = map_.find(name);
    if (it == map_.end()) return std::nullopt;

    const T* result = it->second.get<T>();
    return result ? std::make_optional<T>(*result) : std::nullopt;
  }

  bool has(const std::string& name) const {
    return map_.find(name) != map_.end();
  }

  bool try_str(const std::string& name, std::string& out) const {
    auto it = map_.find(name);
    if (it == map_.end() ||
        it->second.type_name != xtils::type_name<std::string>())
      return false;
    out = *(it->second.get<std::string>());
    return true;
  }
  auto begin() { return map_.begin(); }
  auto end() { return map_.end(); }

  std::size_t size() const { return map_.size(); }

  auto begin() const { return map_.begin(); }
  auto end() const { return map_.end(); }
  std::vector<std::string> keys() const {
    std::vector<std::string> keys;
    for (const auto& pair : map_) {
      keys.push_back(pair.first);
    }
    return keys;
  }
  void clear() { map_.clear(); }

 private:
  std::map<std::string, AnyData> map_;
};

struct IPort {
 public:
  IPort(const std::string& name, int mode, const std::string& type_name)
      : name(name), mode(mode), type_name(type_name) {}
  const std::string name;
  int mode;
  const std::string type_name;
};
using Ports = std::vector<IPort>;

template <typename T>
class InputPort : public IPort {
 public:
  InputPort(const std::string& name)
      : IPort(name, 0, std::string(::xtils::type_name<T>())) {}
};

template <typename T>
class OutputPort : public IPort {
 public:
  OutputPort(const std::string& name)
      : IPort(name, 1, std::string(::xtils::type_name<T>())) {}
};

enum class Status { Success = 0, Failure = 1, Running = 2, Idle = 3 };
enum class Type { Composite = 0, Action = 1, Decorator = 2 };
class BtTree;
class BtFactory;
class BtLogger;
class Node {
 public:
  using Ptr = std::shared_ptr<Node>;
  Node(const std::string& name, Type type)
      : name_(name), type_(type), status_(Status::Idle) {}
  virtual ~Node() = default;
  Status tick();
  virtual Status OnTick() = 0;
  virtual Status OnStart() { return Status::Running; }
  virtual void OnStop() {}
  virtual void reset();
  Type getType() const { return type_; }
  Status getStatus() const { return status_; }
  std::string getName() const { return name_; }
  int getId() const { return id_; }

  static Ports getPorts() { return {}; }
  static std::string desc() { return {}; }

  template <typename T>
  std::optional<T> getInput(const std::string& name) {
    std::string str_val;
    if (data_.try_str(name, str_val)) {
      if (!str_val.empty() && str_val[0] == '&' && blackboard_ &&
          blackboard_->has(str_val.substr(1))) {
        return blackboard_->get<T>(str_val.substr(1));
      }
      if constexpr (std::is_same_v<T, std::string>) {
        return str_val;
      }
    }

    return data_.get<T>(name);
  }

  template <typename T>
  void setOutput(const std::string& name, const T& t) {
    std::string str_val;
    if (data_.try_str(name, str_val)) {
      if (!str_val.empty() && str_val[0] == '&' && blackboard_) {
        blackboard_->set<T>(str_val.substr(1), t);
        return;
      }
    }
    return data_.set(name, t);
  }

 protected:
  friend class BtTree;
  friend class BtFactory;
  std::vector<Node::Ptr> children;
  AnyMap* blackboard_{nullptr};
  AnyMap data_;
  BtLogger* record_{nullptr};
  BtTree* tree_{nullptr};  // Reference to owning tree for event access

 private:
  Type type_;
  int id_;
  std::string name_;
  bool started_{false};
  Status status_;
};

class BtLogger {
 public:
  virtual void update(const Json& tree) {}
  virtual void record(const Node&, Status from, Status to) {}
};

// Composite nodes
class Composite : public Node {
 public:
  Composite(const std::string& name = "");
  void addChild(const Node::Ptr& c) { children.push_back(c); }
  void reset() override final;

 protected:
  size_t current_ = 0;
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
  static Ports getPorts() { return {InputPort<double>("delay_ms")}; }

 private:
  int delay_ms_;
  int start_time_;
};

class BtTree {
 public:
  using Ptr = std::shared_ptr<BtTree>;
  explicit BtTree(Node::Ptr root, const std::string& name = "",
                  std::shared_ptr<AnyMap> blackboard = nullptr,
                  std::shared_ptr<BtLogger> logger = nullptr);
  // no copy/move
  BtTree(const BtTree&) = delete;
  BtTree& operator=(const BtTree&) = delete;

  Status tick();
  void reset();
  void shutdown();
  std::string dump();
  Json dumpTree();

  AnyMap& blackboard() {
    if (!blackboard_) {
      blackboard_ = std::make_shared<AnyMap>();
    }
    return *blackboard_;
  }

  // === Pause/Resume ===
  void pause();
  void resume();
  bool isPaused() const { return paused_.load(); }

  // === Event System ===
  // Send an event to the tree's event queue
  void sendEvent(EventType type, const AnyData& data = {});
  // Peek at an event without consuming it
  std::optional<BtEvent> peekEvent(EventType type) const;
  // Consume and return an event if present
  std::optional<BtEvent> consumeEvent(EventType type);
  // Check if an event of the given type exists
  bool hasEvent(EventType type) const;
  // Clear all pending events
  void clearEvents();

 private:
  void visit_nodes(Node::Ptr& node);
  void inheritRuntimeContext(BtTree* event_tree,
                             const std::shared_ptr<AnyMap>& blackboard,
                             BtLogger* logger);
  std::string dump_node(const Node& node);
  Json dump_tree_node(const Node& node);
  std::atomic_int ids_{0};
  Node::Ptr root_;
  std::string name_;
  std::vector<Node::Ptr> nodes_;
  std::shared_ptr<AnyMap> blackboard_;
  std::shared_ptr<BtLogger> logger_;

  // Pause state
  std::atomic<bool> paused_{false};

  // Event queue with thread-safe access
  mutable std::mutex event_mutex_;
  std::deque<BtEvent> event_queue_;
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
                    type, T::getPorts()};
  }
  void RegisterSimpleAction(std::function<Status()> func,
                            const std::string& name) {
    nodes_[name] = {[func](const std::string& n) {
                      return std::make_shared<SimpleAction>(func, n);
                    },
                    Type::Action};
  }

  // Register a subtree template by name for SubTree node reference
  void RegisterTree(const std::string& name, const Json& tree_json);
  void LoadTreeFile(const std::string& path);
  size_t LoadTreesFromDirectory(const std::string& directory);

  // Get a registered tree JSON by name
  std::optional<Json> getRegisteredTree(const std::string& name) const;
  BtTree::Ptr buildFromRegisteredTree(
      const std::string& name, std::shared_ptr<AnyMap> blackboard = nullptr,
      std::shared_ptr<BtLogger> logger = nullptr);

  // Build tree from json
  BtTree::Ptr buildFromJson(const Json& j,
                            std::shared_ptr<AnyMap> blackboard = nullptr,
                            std::shared_ptr<BtLogger> logger = nullptr);
  std::string dump() const;

 private:
  struct Factory {
    std::function<Node::Ptr(const std::string&)> create;
    Type type;
    Ports ports;
  };
  std::unordered_map<std::string, Factory> nodes_;
  std::unordered_map<std::string, Json> registered_trees_;
  Node::Ptr buildNode(const Json& j);
};

// ============================================================================
// SubTree node - executes a registered or inline subtree
// ============================================================================
class SubTree : public Decorator {
 public:
  SubTree(const std::string& name = "");

  static Ports getPorts() { return {InputPort<std::string>("tree_name")}; }
  static std::string desc() { return "Execute a registered subtree"; }

  Status OnStart() override;
  Status OnTick() override;
  void OnStop() override;

 private:
  friend class BtFactory;
  friend class BtTree;
  BtTree::Ptr subtree_;
  Json inline_tree_;  // For inline subtree definition
};

// ============================================================================
// WaitForEvent - blocks until specified event is received or timeout
// ============================================================================
class WaitForEvent : public ActionNode {
 public:
  WaitForEvent(const std::string& name = "");

  static Ports getPorts() {
    return {
        InputPort<int32_t>("event_type"),
        InputPort<double>("timeout_ms")  // -1 for infinite wait
    };
  }
  static std::string desc() { return "Wait for a specific event"; }

  Status OnStart() override;
  Status OnTick() override;

 private:
  EventType event_type_{0};
  double timeout_ms_{-1};
  uint64_t start_time_{0};
};

// ============================================================================
// EventGuard - monitors events and can interrupt child execution
// ============================================================================
class EventGuard : public Decorator {
 public:
  EventGuard(const std::string& name = "");

  static Ports getPorts() {
    return {
        InputPort<int32_t>("event_type"),
        InputPort<int>("interrupt_mode"),  // 0: next tick, 1: immediate
        InputPort<int>("return_status")    // 0: Success, 1: Failure
    };
  }
  static std::string desc() { return "Guard child with event interrupt"; }

  Status OnStart() override;
  Status OnTick() override;
  void OnStop() override;

 private:
  EventType event_type_{0};
  bool immediate_interrupt_{false};
  Status interrupt_status_{Status::Failure};
  bool interrupted_{false};
};

}  // namespace xtils
