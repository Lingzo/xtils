#include "xtils/fsm/behavior_tree.h"
#include "xtils/fsm/bt_compositelogger.h"
#include "xtils/fsm/bt_filelogger.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// ---------- helpers ----------

static std::vector<Json> read_jsonl(const std::string& path) {
  std::vector<Json> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) {
      auto parsed = Json::parse(line);
      if (parsed) lines.push_back(*parsed);
    }
  }
  return lines;
}

// A mock logger that records every call for verification.
class MockLogger : public BtLogger {
 public:
  std::vector<std::string> calls;

  void update(const Json&) override { calls.push_back("update"); }
  void record(const Node&, Status, Status) override {
    calls.push_back("record");
  }
  void onTickBegin(uint64_t tick) override {
    calls.push_back("tick_begin:" + std::to_string(tick));
  }
  void onTickEnd(uint64_t tick, Status) override {
    calls.push_back("tick_end:" + std::to_string(tick));
  }
};

// ---------- BtFileLogger ----------

TEST_CASE("BtFileLogger: structured JSONL output") {
  const std::string path = "/tmp/bt_logger_test.jsonl";
  auto logger = std::make_shared<BtFileLogger>(path);

  // Simulate tree construction → update()
  Json tree;
  tree["name"] = "test_tree";
  tree["root"] = Json::object_t{};
  logger->update(tree);

  // Build a minimal tree and tick it
  auto root = std::make_shared<AlwaysSuccess>("root_ok");
  BtTree bt(root, "test", nullptr, logger);

  Status result = bt.tick();
  CHECK(result == Status::Success);

  // Read back the file
  auto lines = read_jsonl(path);

  // Expect: update(manual), update(from BtTree ctor), tick_begin, transition,
  // tick_end
  REQUIRE(lines.size() >= 4);

  // First line: our manual update
  CHECK(lines[0]["type"].as_string() == "tree");
  CHECK(lines[0]["data"]["name"].as_string() == "test_tree");

  // Second line: BtTree constructor auto-update
  CHECK(lines[1]["type"].as_string() == "tree");

  // Find tick_begin
  bool found_tick_begin = false;
  bool found_tick_end = false;
  bool found_transition = false;
  for (const auto& l : lines) {
    auto type = l["type"].as_string();
    if (type == "tick_begin") {
      found_tick_begin = true;
      CHECK(l["tick"].as_integer() == 1);
    } else if (type == "tick_end") {
      found_tick_end = true;
      CHECK(l["tick"].as_integer() == 1);
      CHECK(l["result"].as_string() == "Success");
    } else if (type == "transition") {
      found_transition = true;
      CHECK(l["tick"].as_integer() == 1);
      CHECK(l.contains("nid"));
      CHECK(l.contains("name"));
      CHECK(l.contains("from"));
      CHECK(l.contains("to"));
    }
  }
  CHECK(found_tick_begin);
  CHECK(found_tick_end);
  CHECK(found_transition);

  // All lines must have a timestamp
  for (const auto& l : lines) {
    CHECK(l.contains("ts"));
    CHECK(l["ts"].as_integer() > 0);
  }

  std::remove(path.c_str());
}

TEST_CASE("BtFileLogger: multiple ticks produce incrementing tick IDs") {
  const std::string path = "/tmp/bt_logger_ticks.jsonl";
  auto logger = std::make_shared<BtFileLogger>(path);
  auto root = std::make_shared<AlwaysSuccess>("ok");
  BtTree bt(root, "multi_tick", nullptr, logger);

  bt.tick();
  bt.tick();
  bt.tick();

  auto lines = read_jsonl(path);

  int tick_begins = 0;
  int64_t last_tick = 0;
  for (const auto& l : lines) {
    if (l["type"].as_string() == "tick_begin") {
      tick_begins++;
      CHECK(l["tick"].as_integer() > last_tick);
      last_tick = l["tick"].as_integer();
    }
  }
  CHECK(tick_begins == 3);

  std::remove(path.c_str());
}

// ---------- BtCompositeLogger ----------

TEST_CASE("BtCompositeLogger: forwards to all loggers") {
  auto m1 = std::make_shared<MockLogger>();
  auto m2 = std::make_shared<MockLogger>();

  auto composite = std::make_shared<BtCompositeLogger>();
  composite->Add(m1);
  composite->Add(m2);

  auto root = std::make_shared<AlwaysSuccess>("ok");
  BtTree bt(root, "composite_test", nullptr, composite);

  bt.tick();

  // Both loggers should have received: update (from ctor), tick_begin, record,
  // tick_end
  for (auto& m : {m1, m2}) {
    CHECK(m->calls.size() >= 4);
    CHECK(m->calls[0] == "update");
    CHECK(m->calls[1] == "tick_begin:1");
    // record is in the middle
    bool has_record = false;
    for (auto& c : m->calls) {
      if (c == "record") has_record = true;
    }
    CHECK(has_record);
    CHECK(m->calls.back() == "tick_end:1");
  }
}

TEST_CASE("BtCompositeLogger: empty composite does not crash") {
  auto composite = std::make_shared<BtCompositeLogger>();
  auto root = std::make_shared<AlwaysFailure>("fail");
  BtTree bt(root, "empty_composite", nullptr, composite);
  CHECK(bt.tick() == Status::Failure);
}

// ---------- Tick lifecycle ----------

TEST_CASE("BtLogger: tick lifecycle order") {
  auto mock = std::make_shared<MockLogger>();
  auto root = std::make_shared<AlwaysSuccess>("ok");
  BtTree bt(root, "lifecycle", nullptr, mock);

  bt.tick();

  // Expected order: update, tick_begin:1, record, tick_end:1
  REQUIRE(mock->calls.size() >= 4);
  CHECK(mock->calls[0] == "update");
  CHECK(mock->calls[1] == "tick_begin:1");
  // record(s) between
  CHECK(mock->calls.back() == "tick_end:1");
}

TEST_CASE("BtLogger: paused tick does not trigger logger") {
  auto mock = std::make_shared<MockLogger>();
  auto root = std::make_shared<AlwaysSuccess>("ok");
  BtTree bt(root, "paused", nullptr, mock);

  mock->calls.clear();  // clear the update from ctor
  bt.pause();
  Status s = bt.tick();
  CHECK(s == Status::Running);
  // No tick_begin/tick_end should be logged
  CHECK(mock->calls.empty());
}

