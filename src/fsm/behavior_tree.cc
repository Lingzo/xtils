#include <xtils/fsm/behavior_tree.h>
#include <xtils/logging/logger.h>
#include <xtils/utils/exception.h>

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
                                     std::shared_ptr<AnyMap> blackboard) {
  if (!json.has_key("root")) {
    throw xtils::runtime_error("Node JSON must have a root");
  }
  auto j = json["root"];

  auto root = buildNode(j);
  auto tree = std::make_shared<BtTree>(root, "BehaviorTree", blackboard);
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
      if (!j.has_key("children") || j["children"].as_array().size() != 1) {
        throw xtils::runtime_error(
            "Decorator node must have exactly one child");
      }
      std::reinterpret_pointer_cast<Decorator>(node)->setChild(
          buildNode(j["children"][0]));
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
    return node;
  } else {
    throw xtils::runtime_error("Unknown node type: " + id);
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
  Register<Inverter>("Inverter");
  Register<Retry>("Retry");
  Register<Repeater>("Repeater");

  // Actions
  Register<AlwaysSuccess>("AlwaysSuccess");
  Register<AlwaysFailure>("AlwaysFailure");
  Register<Wait>("Wait");
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
  if (!started_) {
    started_ = true;
    status_ = OnStart();
  }
  if (status_ == Status::Running) status_ = OnTick();
  if (status_ == Status::Success || status_ == Status::Failure) {
    started_ = false;
    OnStop();
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
  if (!children.empty()) return Status::Failure;
  if (steady::GetCurrentMs() - start_time_ < delay_ms_) {
    return Status::Running;
  } else {
    return children[0]->tick();
  }
}

// Inverter node
Inverter::Inverter(const std::string& name) : Decorator(name) {}
Status Inverter::OnTick() {
  if (!children.empty()) return Status::Failure;
  Status s = children[0]->tick();
  if (s == Status::Running) return Status::Running;
  if (s == Status::Success) return Status::Failure;
  return Status::Success;
}

// BtTree
BtTree::BtTree(Node::Ptr root, const std::string& name,
               std::shared_ptr<AnyMap> blackboard)
    : root_(root), name_(name) {
  if (blackboard) {
    blackboard_ = blackboard;
  } else {
    blackboard_ = std::make_shared<AnyMap>();
  }
  visit_nodes(root_);
}
void BtTree::visit_nodes(Node::Ptr& node) {
  nodes_.push_back(node);
  node->id_ = ids_.fetch_add(1);
  node->blackboard_ = blackboard_.get();
  for (auto& child : node->children) {
    visit_nodes(child);
  }
}
Status BtTree::tick() { return root_->tick(); }
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

}  // namespace xtils
