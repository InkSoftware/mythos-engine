#include "engine/editor/event_graph.h"

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <utility>

namespace mythos::editor {
namespace {
GraphNodeKind KindOf(const EventKind kind) {
  switch (kind) {
    case EventKind::kTrigger:
      return GraphNodeKind::kTrigger;
    case EventKind::kFlag:
      return GraphNodeKind::kFlag;
    default:
      return GraphNodeKind::kTimer;
  }
}

size_t NodeFor(EventGraph& g, const GraphNodeKind kind, const std::string& id) {
  const int found = g.Find(kind, id);
  if (found >= 0) return static_cast<size_t>(found);
  GraphNode n;
  n.kind = kind;
  n.id = id;
  g.nodes.push_back(std::move(n));
  return g.nodes.size() - 1;
}
}

int EventGraph::Find(const GraphNodeKind kind, const std::string& id) const {
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].kind == kind && nodes[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

int EventGraph::Columns() const {
  int c = 0;
  for (const GraphNode& n : nodes) c = std::max(c, n.column + 1);
  return c;
}

EventGraph BuildEventGraph(const LevelDoc& doc) {
  EventGraph g;
  for (const Trigger& t : doc.triggers)
    NodeFor(g, GraphNodeKind::kTrigger, t.id);
  for (const EventRule& r : doc.rules) {
    const size_t rule = NodeFor(g, GraphNodeKind::kRule, r.id);
    for (const EventAction& a : r.actions) {
      g.nodes[rule].lines.push_back(
          a.argument.empty() ? a.type : a.type + " " + a.argument);
    }
    if (!r.event.id.empty()) {
      const size_t from = NodeFor(g, KindOf(r.event.kind), r.event.id);
      std::string label = r.event.kind == EventKind::kTrigger ? r.event.detail
                          : r.event.kind == EventKind::kFlag
                              ? (r.event.value ? "on" : "off")
                              : "";
      g.edges.push_back({GraphEdgeKind::kStarts, from, rule, label});
    }
    for (const EventCondition& c : r.conditions) {
      const size_t from = NodeFor(g, GraphNodeKind::kFlag, c.flag);
      g.edges.push_back(
          {GraphEdgeKind::kCondition, from, rule, c.value ? "if" : "if not"});
    }
    for (const EventAction& a : r.actions) {
      if (a.type != "set_flag" || a.argument.empty()) continue;
      const size_t to = NodeFor(g, GraphNodeKind::kFlag, a.argument);
      g.edges.push_back({GraphEdgeKind::kSets, rule, to, ""});
    }
  }

  // Columns: the longest chain of edges leading to a node. A walk from what
  // starts the chains (then from whatever is left) finds the edges that close a
  // loop (back into a node still on the walk); those are ignored, so a loop of
  // flags keeps the order of its first link.
  const size_t n = g.nodes.size();
  std::vector<bool> has_in(n, false), back(g.edges.size(), false);
  for (const GraphEdge& e : g.edges) has_in[e.to] = true;
  std::vector<int> state(n, 0);  // 0 new, 1 on the walk, 2 done
  const std::function<void(size_t)> walk = [&](const size_t from) {
    state[from] = 1;
    for (size_t i = 0; i < g.edges.size(); ++i) {
      if (g.edges[i].from != from) continue;
      const size_t to = g.edges[i].to;
      if (state[to] == 1) {
        back[i] = true;
      } else if (state[to] == 0) {
        walk(to);
      }
    }
    state[from] = 2;
  };
  for (size_t v = 0; v < n; ++v)
    if (!has_in[v] && state[v] == 0) walk(v);
  for (size_t v = 0; v < n; ++v)
    if (state[v] == 0) walk(v);
  for (size_t pass = 0; pass < n; ++pass) {
    bool changed = false;
    for (size_t i = 0; i < g.edges.size(); ++i) {
      if (back[i]) continue;
      const int want = g.nodes[g.edges[i].from].column + 1;
      if (g.nodes[g.edges[i].to].column < want) {
        g.nodes[g.edges[i].to].column = want;
        changed = true;
      }
    }
    if (!changed) break;
  }
  // Rows: the order of appearance inside a column.
  std::map<int, int> next_row;
  for (GraphNode& node : g.nodes) node.row = next_row[node.column]++;
  return g;
}

bool Connect(LevelDoc& doc, const GraphNodeKind kind, const std::string& id,
             const std::string& rule_id) {
  if (kind == GraphNodeKind::kRule) return false;
  const auto rule = std::ranges::find_if(
      doc.rules, [&](const EventRule& r) { return r.id == rule_id; });
  if (rule == doc.rules.end() || id.empty()) return false;
  if (kind == GraphNodeKind::kTrigger) {
    const auto trigger = std::ranges::find_if(
        doc.triggers, [&](const Trigger& t) { return t.id == id; });
    if (trigger == doc.triggers.end()) return false;
    // The detail is the event the trigger emits.
    const char* const detail = trigger->event == TriggerEvent::kExit   ? "exit"
                               : trigger->event == TriggerEvent::kStay ? "stay"
                               : trigger->event == TriggerEvent::kUse ? "use"
                                                                      : "enter";
    rule->event = {EventKind::kTrigger, id, detail, false};
  } else if (kind == GraphNodeKind::kFlag) {
    rule->event = {EventKind::kFlag, id, "", true};
  } else {
    rule->event = {EventKind::kTimer, id, "", false};
  }
  return true;
}

namespace {
EventRule* FindRule(LevelDoc& doc, const std::string& id) {
  const auto it = std::ranges::find_if(
      doc.rules, [&](const EventRule& r) { return r.id == id; });
  return it == doc.rules.end() ? nullptr : &*it;
}
}

bool AddSetFlag(LevelDoc& doc, const std::string& rule_id,
                const std::string& flag) {
  EventRule* r = FindRule(doc, rule_id);
  if (r == nullptr || flag.empty()) return false;
  const EventAction action{"set_flag", flag};
  if (std::ranges::find(r->actions, action) == r->actions.end())
    r->actions.push_back(action);
  return true;
}

bool AddCondition(LevelDoc& doc, const std::string& rule_id,
                  const std::string& flag, const bool value) {
  EventRule* r = FindRule(doc, rule_id);
  if (r == nullptr || flag.empty()) return false;
  for (EventCondition& c : r->conditions) {
    if (c.flag == flag) {
      c.value = value;
      return true;
    }
  }
  r->conditions.push_back({flag, value});
  return true;
}

bool RemoveCondition(LevelDoc& doc, const std::string& rule_id,
                     const std::string& flag) {
  EventRule* r = FindRule(doc, rule_id);
  if (r == nullptr) return false;
  return std::erase_if(r->conditions, [&](const EventCondition& c) {
           return c.flag == flag;
         }) > 0;
}

bool DeleteRule(LevelDoc& doc, const std::string& rule_id) {
  return std::erase_if(doc.rules,
                       [&](const EventRule& r) { return r.id == rule_id; }) > 0;
}

std::string AddRuleFrom(LevelDoc& doc, const GraphNodeKind kind,
                        const std::string& id) {
  if (kind == GraphNodeKind::kRule) return {};
  EventRule rule;
  rule.id = NewRuleId(doc);
  doc.rules.push_back(rule);
  if (!Connect(doc, kind, id, rule.id)) {
    doc.rules.pop_back();
    return {};
  }
  return rule.id;
}

std::string SerializeGraphLayout(const std::vector<GraphPlacement>& nodes) {
  std::string s;
  for (const GraphPlacement& n : nodes) {
    s += std::to_string(n.kind) + " " + n.id + " " + FormatNumber(n.x) + " " +
         FormatNumber(n.y) + "\n";
  }
  return s;
}

std::vector<GraphPlacement> ParseGraphLayout(const std::string& text) {
  std::vector<GraphPlacement> out;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    std::istringstream ls(line);
    GraphPlacement p;
    if (ls >> p.kind >> p.id >> p.x >> p.y) out.push_back(std::move(p));
  }
  return out;
}
}
