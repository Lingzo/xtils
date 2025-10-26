#include <xtils/fsm/behavior_tree.h>
#include <xtils/logging/logger.h>
#include <xtils/utils/exception.h>

#include <memory>
#include <sstream>
#include <string>

namespace xtils {

Status Sequence::OnTick() {
  while (current < children.size()) {
    Status s = children[current]->tick();
    if (s == Status::Running) return Status::Running;
    if (s == Status::Failure) {
      current = 0;
      return Status::Failure;
    }
    // success -> move to next child
    ++current;
  }
  current = 0;
  return Status::Success;
}

Status Selector::OnTick() {
  while (current < children.size()) {
    Status s = children[current]->tick();
    if (s == Status::Running) return Status::Running;
    if (s == Status::Success) {
      current = 0;
      return Status::Success;
    }
    // failure -> try next child
    ++current;
  }
  current = 0;
  return Status::Failure;
}

BtTree::Ptr BtFactory::buildFromJson(const Json& json) {
  if (!json.has_key("root")) {
    throw xtils::runtime_error("Node JSON must have a root");
  }
  auto j = json["root"];

  auto root = buildNode(j);
  auto tree = std::make_shared<BtTree>(root);
  return tree;
}

Node::Ptr BtFactory::buildNode(const Json& j) {
  if (!j.is_object()) throw xtils::runtime_error("Node JSON must be object");
  std::string id = j.get_string("id").value_or("");
  std::string name = j.get_string("name").value_or("");
  if (id.empty()) throw xtils::runtime_error("Node must have an id");
  if (name.empty()) {
    name = id;
  }
  auto factory = nodes_.find(id);
  if (factory != nodes_.end()) {
    auto node = factory->second.create(name);
    if (factory->second.type == Type::Composite) {
      if (!j.has_key("children")) {
        throw xtils::runtime_error("Composite node must have children");
      }
      for (auto& c : j["children"].as_array())
        std::reinterpret_pointer_cast<Composite>(node)->addChild(buildNode(c));
    } else if (factory->second.type == Type::Decorator) {
      if (!j.has_key("child")) {
        throw xtils::runtime_error(
            "Decorator node must have exactly one child");
      }
      std::reinterpret_pointer_cast<Decorator>(node)->setChild(
          buildNode(j["child"]));
    }
    return node;
  } else {
    throw xtils::runtime_error("Unknown node type: " + id);
  }
}

BtFactory::BtFactory() {
  Register<Selector>("Selector");
  Register<Sequence>("Sequence");
  Register<AlwaysSuccess>("AlwaysSuccess");
  Register<AlwaysFailure>("AlwaysFailure");
  Register<Delay>("Delay");
  Register<Inverter>("Inverter");
}

std::string BtFactory::dump() const {
  Json arr;
  for (const auto& [id, factory] : nodes_) {
    Json jsObj;
    jsObj["id"] = id;
    jsObj["type"] = static_cast<int>(factory.type);
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
  delay_ = 100;
  return Status::Running;
}
Status Delay::OnTick() {
  if (!children.empty()) return Status::Failure;
  if (delay_ > 0) {
    delay_--;
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
BtTree::BtTree(Node::Ptr root, const std::string& name)
    : root_(root), name_(name) {
  set_node_id(root_);
}
void BtTree::set_node_id(Node::Ptr node) {
  nodes_.push_back(node);
  node->id_ = ids_.fetch_add(1);
  for (auto& child : node->children) {
    set_node_id(child);
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
  ss << dump_node(root_);
  ss << "}\n";
  return ss.str();
}

std::string BtTree::dump_node(Node::Ptr node) {
  std::stringstream ss;

  for (auto& child : node->children) {
    ss << node->id_ << "->" << child->id_ << ";\n";
    ss << dump_node(child);
  }
  return ss.str();
}

}  // namespace xtils
