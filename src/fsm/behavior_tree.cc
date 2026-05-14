#include <xtils/fsm/behavior_tree.h>
#include <xtils/logging/logger.h>
#include <xtils/utils/exception.h>
#include <xtils/utils/file_utils.h>
#include <xtils/utils/time_utils.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "nodes.h"
#include "xtils/utils/type_traits.h"

namespace xtils {

Status Sequence::OnTick() {
  while (current_ < children.size()) {
    Status s = children[current_]->tick();
    if (s == Status::Running) return Status::Running;
    if (s == Status::Failure) {
      current_ = 0;
      return Status::Failure;
    }
    // success -> move to next child
    ++current_;
  }
  current_ = 0;
  return Status::Success;
}

Status Selector::OnTick() {
  while (current_ < children.size()) {
    Status s = children[current_]->tick();
    if (s == Status::Running) return Status::Running;
    if (s == Status::Success) {
      current_ = 0;
      return Status::Success;
    }
    // failure -> try next child
    ++current_;
  }
  current_ = 0;
  return Status::Failure;
}

BtTree::Ptr BtFactory::buildFromJson(const Json& json,
                                     std::shared_ptr<AnyMap> blackboard,
                                     std::shared_ptr<BtLogger> logger) {
  if (!json.has_key("root")) {
    throw xtils::runtime_error("Node JSON must have a root");
  }
  auto j = json["root"];
  auto tree_name = json.get_string("name").value_or("BehaviorTree");

  auto root = buildNode(j);
  auto tree = std::make_shared<BtTree>(root, tree_name, blackboard, logger);
  return tree;
}

Node::Ptr BtFactory::buildNode(const Json& j) {
  if (!j.is_object()) throw xtils::runtime_error("Node JSON must be object");
  std::string id = j.get_string("id").value_or("");
  std::string name = j.get_string("name").value_or("");
  if (id.empty() && name.empty())
    throw xtils::runtime_error("Node must have an id or name");
  if (name.empty()) {
    name = id;
  }
  auto factory = nodes_.find(name);
  if (factory != nodes_.end()) {
    auto node = factory->second.create(name);
    if (factory->second.type == Type::Composite) {
      if (!j.has_key("children")) {
        throw xtils::runtime_error("Composite node must have children");
      }
      for (auto& c : j["children"].as_array())
        std::reinterpret_pointer_cast<Composite>(node)->addChild(buildNode(c));
    } else if (factory->second.type == Type::Decorator) {
      // Special handling for SubTree node
      if (name == "SubTree") {
        auto subtree_node = std::reinterpret_pointer_cast<SubTree>(node);

        // Check for inline subtree definition
        if (j.has_key("subtree")) {
          auto subtree_json = j["subtree"];
          subtree_node->inline_tree_ = subtree_json;
          // Build the subtree immediately
          subtree_node->subtree_ = buildFromJson(subtree_json);
        }
        // For registered tree_name, the subtree will be built in OnStart
        // after ports are processed below
      } else {
        // Normal decorator: must have exactly one child
        if (!j.has_key("children") || j["children"].as_array().size() != 1) {
          throw xtils::runtime_error(
              "Decorator node must have exactly one child");
        }
        std::reinterpret_pointer_cast<Decorator>(node)->setChild(
            buildNode(j["children"][0]));
      }
    }

    if (auto ports = j.get_object("ports")) {
      const auto& node_ports = factory->second.ports;
      for (auto& p : ports.value()) {
        const std::string& name = p.first;
        const Json& value = p.second;
        auto it = std::find_if(node_ports.begin(), node_ports.end(),
                               [&name](auto& e) { return e.name == name; });
        if (it != node_ports.end()) {
          if (value.is_number()) {
            if (it->type_name == xtils::type_name<int>()) {
              node->data_.set<int>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<int64_t>()) {
              node->data_.set<int64_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<uint64_t>()) {
              node->data_.set<uint64_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<uint32_t>()) {
              node->data_.set<uint32_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<uint16_t>()) {
              node->data_.set<uint16_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<uint8_t>()) {
              node->data_.set<uint8_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<int32_t>()) {
              node->data_.set<int32_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<int16_t>()) {
              node->data_.set<int16_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<int8_t>()) {
              node->data_.set<int8_t>(name, value.as_integer());
            } else if (it->type_name == xtils::type_name<float>()) {
              node->data_.set<float>(name, value.as_number());
            } else if (it->type_name == xtils::type_name<double>()) {
              node->data_.set<double>(name, value.as_number());
            } else {
              LogW("Unsupported type: %s", it->type_name.c_str());
            }
          } else if (value.is_bool()) {
            node->data_.set(name, value.as_bool());
          } else if (value.is_string()) {
            node->data_.set(name, value.as_string());
          } else {
            LogW("Unsupported prot: %s", value.dump().c_str());
          }
        } else {
          LogW("Unsupported prot: %s", value.dump().c_str());
        }
      }
    }

    // Handle SubTree with registered tree_name (after ports are processed)
    if (name == "SubTree") {
      auto subtree_node = std::reinterpret_pointer_cast<SubTree>(node);
      if (!subtree_node->subtree_) {
        // No inline subtree, try to build from registered tree_name
        auto tree_name_opt = subtree_node->getInput<std::string>("tree_name");
        if (tree_name_opt && !tree_name_opt->empty()) {
          auto registered_tree = getRegisteredTree(*tree_name_opt);
          if (registered_tree) {
            subtree_node->subtree_ = buildFromJson(*registered_tree);
          } else {
            throw xtils::runtime_error("SubTree: registered tree not found: " +
                                       *tree_name_opt);
          }
        }
      }
    }

    return node;
  } else {
    throw xtils::runtime_error("Unknown node type: " + name);
  }
}

BtFactory::BtFactory() {
  // composites
  Register<Selector>("Selector");
  Register<RandomSelector>("RandomSelector");
  Register<Sequence>("Sequence");
  Register<Fallback>("Fallback");

  // decorators
  Register<Delay>("Delay");
  Register<Timeout>("Timeout");
  Register<Inverter>("Inverter");
  Register<Retry>("Retry");
  Register<Repeater>("Repeater");
  Register<SubTree>("SubTree");
  Register<EventGuard>("EventGuard");

  // Actions
  Register<AlwaysSuccess>("AlwaysSuccess");
  Register<AlwaysFailure>("AlwaysFailure");
  Register<Wait>("Wait");
  Register<WaitForEvent>("WaitForEvent");
}

std::string BtFactory::dump() const {
  Json arr;
  for (const auto& [name, factory] : nodes_) {
    Json jsObj;
    jsObj["name"] = name;
    jsObj["type"] = static_cast<int>(factory.type);
    jsObj["ports"];
    for (auto& p : factory.ports) {
      Json port;
      port["name"] = p.name;
      port["mode"] = p.mode;
      port["type_name"] = p.type_name;
      jsObj["ports"].push_back(port);
    }
    arr.push_back(jsObj);
  }
  Json json;
  json["nodes"] = arr;
  return json.dump(2);
}

// decorator node
Decorator::Decorator(const std::string& name) : Node(name, Type::Decorator) {}
void Decorator::reset() { Node::reset(); }

// action node
ActionNode::ActionNode(const std::string& name) : Node(name, Type::Action) {}
void ActionNode::reset() { Node::reset(); }

// composite node
Composite::Composite(const std::string& name) : Node(name, Type::Composite) {}

void Composite::reset() {
  for (auto& c : children) c->reset();
  Node::reset();
  current_ = 0;
}

Status Node::tick() {
  Status pre_status = status_;
  if (!started_) {
    started_ = true;
    status_ = OnStart();
  }
  if (status_ == Status::Running) status_ = OnTick();
  if (status_ == Status::Success || status_ == Status::Failure) {
    started_ = false;
    OnStop();
  }
  if (record_) {
    record_->record(*this, pre_status, status_);
  }
  return status_;
}

void Node::reset() {
  started_ = false;
  status_ = Status::Idle;
  for (auto& c : children) c->reset();
}

Delay::Delay(const std::string& name) : Decorator(name) {}

Status Delay::OnStart() {
  auto delay_opt = getInput<double>("delay_ms");
  if (!delay_opt) throw xtils::runtime_error("Delay Requires delay_ms input");
  delay_ms_ = *delay_opt;
  start_time_ = steady::GetCurrentMs();
  return Status::Running;
}
Status Delay::OnTick() {
  if (children.empty()) return Status::Failure;
  if (steady::GetCurrentMs() - start_time_ < delay_ms_) {
    return Status::Running;
  } else {
    return children[0]->tick();
  }
}

// Inverter node
Inverter::Inverter(const std::string& name) : Decorator(name) {}
Status Inverter::OnTick() {
  if (children.empty()) return Status::Failure;
  Status s = children[0]->tick();
  if (s == Status::Running) return Status::Running;
  if (s == Status::Success) return Status::Failure;
  return Status::Success;
}

// BtTree
BtTree::BtTree(Node::Ptr root, const std::string& name,
               std::shared_ptr<AnyMap> blackboard,
               std::shared_ptr<BtLogger> logger)
    : root_(root), name_(name), logger_(logger) {
  if (blackboard) {
    blackboard_ = blackboard;
  } else {
    blackboard_ = std::make_shared<AnyMap>();
  }
  visit_nodes(root_);
  if (logger_) {
    logger_->update(dumpTree());
  }
}
void BtTree::visit_nodes(Node::Ptr& node) {
  nodes_.push_back(node);
  node->id_ = ids_.fetch_add(1);
  node->blackboard_ = blackboard_.get();
  node->tree_ = this;  // Set tree reference for event access
  node->record_ = logger_ ? logger_.get() : nullptr;
  if (auto subtree_node = std::dynamic_pointer_cast<SubTree>(node);
      subtree_node && subtree_node->subtree_) {
    subtree_node->subtree_->inheritRuntimeContext(this, blackboard_,
                                                  node->record_);
  }
  for (auto& child : node->children) {
    visit_nodes(child);
  }
}

void BtTree::inheritRuntimeContext(BtTree* event_tree,
                                   const std::shared_ptr<AnyMap>& blackboard,
                                   BtLogger* logger) {
  blackboard_ = blackboard;
  for (auto& node : nodes_) {
    node->blackboard_ = blackboard_.get();
    node->tree_ = event_tree;
    node->record_ = logger;
    if (auto subtree_node = std::dynamic_pointer_cast<SubTree>(node);
        subtree_node && subtree_node->subtree_) {
      subtree_node->subtree_->inheritRuntimeContext(event_tree, blackboard,
                                                    logger);
    }
  }
}
Status BtTree::tick() {
  // When paused, maintain Running state without executing
  if (paused_.load()) {
    return Status::Running;
  }
  ++tick_count_;
  if (logger_) {
    logger_->onTickBegin(tick_count_);
  }
  Status result = root_->tick();
  if (logger_) {
    logger_->onTickEnd(tick_count_, result);
  }
  return result;
}
void BtTree::reset() { root_->reset(); }

void BtTree::shutdown() {}
std::string BtTree::dump() {
  std::stringstream ss;
  ss << "digraph " << name_ << " {\n";
  for (auto& node : nodes_) {
    ss << node->id_ << "[label=" << node->name_ << "];\n";
  }
  ss << dump_node(*root_);
  ss << "}\n";
  return ss.str();
}

xtils::Json BtTree::dumpTree() {
  Json json;
  json["root"] = dump_tree_node(*root_);
  json["name"] = name_;
  json["version"] = "1.0";
  return json;
}

std::string BtTree::dump_node(const Node& node) {
  std::stringstream ss;

  for (auto& child : node.children) {
    ss << node.id_ << "->" << child->id_ << ";\n";
    ss << dump_node(*child);
  }
  return ss.str();
}

Json BtTree::dump_tree_node(const Node& node) {
  Json json;
  json["name"] = node.name_;
  json["type"] = static_cast<int>(node.type_);
  for (const auto& p : node.data_) {
    auto& value = p.second;
    auto& name = p.first;
    auto& port = json["ports"];
    if (value.type_name == xtils::type_name<int>()) {
      port[name] = node.data_.get<int>(name).value();
    } else if (value.type_name == xtils::type_name<int8_t>()) {
      port[name] = node.data_.get<int8_t>(name).value();
    } else if (value.type_name == xtils::type_name<uint8_t>()) {
      port[name] = node.data_.get<uint8_t>(name).value();
    } else if (value.type_name == xtils::type_name<int16_t>()) {
      port[name] = node.data_.get<int16_t>(name).value();
    } else if (value.type_name == xtils::type_name<uint16_t>()) {
      port[name] = node.data_.get<uint16_t>(name).value();
    } else if (value.type_name == xtils::type_name<int64_t>()) {
      port[name] = node.data_.get<int64_t>(name).value();
    } else if (value.type_name == xtils::type_name<uint64_t>()) {
      port[name] = node.data_.get<uint64_t>(name).value();
    } else if (value.type_name == xtils::type_name<int32_t>()) {
      port[name] = node.data_.get<int32_t>(name).value();
    } else if (value.type_name == xtils::type_name<uint32_t>()) {
      port[name] = node.data_.get<uint32_t>(name).value();
    } else if (value.type_name == xtils::type_name<double>()) {
      port[name] = node.data_.get<double>(name).value();
    } else if (value.type_name == xtils::type_name<float>()) {
      port[name] = node.data_.get<float>(name).value();
    } else if (value.type_name == xtils::type_name<bool>()) {
      port[name] = node.data_.get<bool>(name).value();
    } else if (value.type_name == xtils::type_name<std::string>()) {
      port[name] = node.data_.get<std::string>(name).value();
    } else {
      LogD("unsupport port %s - %s", name.c_str(),
           std::string(value.type_name).c_str());
    }
  }
  for (const auto& n : node.children) {
    json["children"].push_back(dump_tree_node(*n));
  }

  return json;
}

// ============================================================================
// BtTree Pause/Resume
// ============================================================================
void BtTree::pause() { paused_.store(true); }

void BtTree::resume() { paused_.store(false); }

// ============================================================================
// BtTree Event System
// ============================================================================
void BtTree::sendEvent(EventType type, const AnyData& data) {
  std::lock_guard<std::mutex> lock(event_mutex_);
  BtEvent event;
  event.type = type;
  event.data = data;
  event.timestamp = steady::GetCurrentMs();
  event_queue_.push_back(event);
}

std::optional<BtEvent> BtTree::peekEvent(EventType type) const {
  std::lock_guard<std::mutex> lock(event_mutex_);
  for (const auto& event : event_queue_) {
    if (event.type == type) {
      return event;
    }
  }
  return std::nullopt;
}

std::optional<BtEvent> BtTree::consumeEvent(EventType type) {
  std::lock_guard<std::mutex> lock(event_mutex_);
  for (auto it = event_queue_.begin(); it != event_queue_.end(); ++it) {
    if (it->type == type) {
      BtEvent event = *it;
      event_queue_.erase(it);
      return event;
    }
  }
  return std::nullopt;
}

bool BtTree::hasEvent(EventType type) const {
  std::lock_guard<std::mutex> lock(event_mutex_);
  for (const auto& event : event_queue_) {
    if (event.type == type) {
      return true;
    }
  }
  return false;
}

void BtTree::clearEvents() {
  std::lock_guard<std::mutex> lock(event_mutex_);
  event_queue_.clear();
}

// ============================================================================
// BtFactory Tree Registration
// ============================================================================
void BtFactory::RegisterTree(const std::string& name, const Json& tree_json) {
  registered_trees_[name] = tree_json;
  auto tree_name = tree_json.get_string("name").value_or("");
  if (!tree_name.empty()) {
    registered_trees_[tree_name] = tree_json;
  }
}

void BtFactory::LoadTreeFile(const std::string& path) {
  std::string content;
  if (!file_utils::read(path, &content) || content.empty()) {
    throw xtils::runtime_error("Failed to read tree file: " + path);
  }

  auto json = Json::parse(content);
  if (!json.has_value() || !json->has_key("root")) {
    throw xtils::runtime_error("Invalid tree JSON file: " + path);
  }

  RegisterTree(file_utils::stem(path), *json);
}

size_t BtFactory::LoadTreesFromDirectory(const std::string& directory) {
  if (!file_utils::is_directory(directory)) {
    throw xtils::runtime_error("Tree directory not found: " + directory);
  }

  auto files = file_utils::list_files(directory);
  std::sort(files.begin(), files.end());

  size_t loaded = 0;
  for (const auto& file : files) {
    if (file_utils::extension(file) != ".json") {
      continue;
    }
    LoadTreeFile(file_utils::join_path(directory, file));
    ++loaded;
  }

  return loaded;
}

std::optional<Json> BtFactory::getRegisteredTree(
    const std::string& name) const {
  auto it = registered_trees_.find(name);
  if (it != registered_trees_.end()) {
    return it->second;
  }
  return std::nullopt;
}

BtTree::Ptr BtFactory::buildFromRegisteredTree(
    const std::string& name, std::shared_ptr<AnyMap> blackboard,
    std::shared_ptr<BtLogger> logger) {
  auto tree = getRegisteredTree(name);
  if (!tree) {
    throw xtils::runtime_error("Registered tree not found: " + name);
  }
  return buildFromJson(*tree, blackboard, logger);
}

// ============================================================================
// SubTree Node
// ============================================================================
SubTree::SubTree(const std::string& name) : Decorator(name) {}

Status SubTree::OnStart() {
  // Subtree should be initialized by factory or inline definition
  if (!subtree_) {
    return Status::Failure;
  }
  return Status::Running;
}

Status SubTree::OnTick() {
  if (!subtree_) {
    return Status::Failure;
  }
  return subtree_->tick();
}

void SubTree::OnStop() {
  if (subtree_) {
    subtree_->reset();
  }
}

// ============================================================================
// WaitForEvent Node
// ============================================================================
WaitForEvent::WaitForEvent(const std::string& name) : ActionNode(name) {}

Status WaitForEvent::OnStart() {
  auto event_type_opt = getInput<int32_t>("event_type");
  if (!event_type_opt) {
    throw xtils::runtime_error("WaitForEvent requires event_type input");
  }
  event_type_ = *event_type_opt;

  auto timeout_opt = getInput<double>("timeout_ms");
  timeout_ms_ = timeout_opt.value_or(-1);

  start_time_ = steady::GetCurrentMs();
  return Status::Running;
}

Status WaitForEvent::OnTick() {
  if (!tree_) {
    return Status::Failure;
  }

  // Check for event
  if (tree_->consumeEvent(event_type_)) {
    return Status::Success;
  }

  // Check timeout (timeout_ms_ < 0 means infinite wait)
  if (timeout_ms_ >= 0) {
    auto elapsed = steady::GetCurrentMs() - start_time_;
    if (elapsed >= static_cast<uint64_t>(timeout_ms_)) {
      return Status::Failure;
    }
  }

  return Status::Running;
}

// ============================================================================
// EventGuard Node
// ============================================================================
EventGuard::EventGuard(const std::string& name) : Decorator(name) {}

Status EventGuard::OnStart() {
  auto event_type_opt = getInput<int32_t>("event_type");
  if (!event_type_opt) {
    throw xtils::runtime_error("EventGuard requires event_type input");
  }
  event_type_ = *event_type_opt;

  auto mode_opt = getInput<int>("interrupt_mode");
  immediate_interrupt_ = (mode_opt.value_or(0) == 1);

  auto status_opt = getInput<int>("return_status");
  interrupt_status_ =
      (status_opt.value_or(1) == 0) ? Status::Success : Status::Failure;

  interrupted_ = false;
  return Status::Running;
}

Status EventGuard::OnTick() {
  if (!tree_ || children.empty()) {
    return Status::Failure;
  }

  // Check for interrupt event
  if (tree_->hasEvent(event_type_)) {
    tree_->consumeEvent(event_type_);
    interrupted_ = true;

    if (immediate_interrupt_) {
      // Immediate interrupt: stop child and return interrupt status
      children[0]->reset();
      return interrupt_status_;
    }
    // Deferred interrupt: will interrupt on next tick
  }

  // If interrupted in deferred mode, return interrupt status
  if (interrupted_ && !immediate_interrupt_) {
    children[0]->reset();
    return interrupt_status_;
  }

  // Normal execution: tick child
  return children[0]->tick();
}

void EventGuard::OnStop() { interrupted_ = false; }

}  // namespace xtils
