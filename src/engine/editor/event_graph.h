// The level's triggers and rules as a graph for the editor's Event graph
// panel: what starts a rule (a trigger, a flag changing, a timer), the flags
// its conditions read, and the flags its actions set. Headless; the panel
// draws it (`platform/editor/ui_panels.cpp`) and uses `Connect` for rewiring.
//
// An action of type "set_flag" is read as setting the flag its argument names
// (the convention of the template game and the editor's rule panel); any other
// action stays text on its rule.
#ifndef MYTHOS_ENGINE_EDITOR_EVENT_GRAPH_H_
#define MYTHOS_ENGINE_EDITOR_EVENT_GRAPH_H_

#include <string>
#include <vector>

#include "engine/editor/level_doc.h"

namespace mythos::editor {
enum class GraphNodeKind { kTrigger, kTimer, kFlag, kRule };

struct GraphNode {
  GraphNodeKind kind = GraphNodeKind::kTrigger;
  // The trigger, timer, flag or rule id: unique within a kind.
  std::string id;
  // Layout: the column it sits in (0 starts a chain) and the row in it.
  int column = 0, row = 0;
  // A rule's actions, one line each ("type argument").
  std::vector<std::string> lines;
};

enum class GraphEdgeKind {
  kStarts,     // a trigger, timer or flag event starts a rule
  kCondition,  // a flag a rule's condition reads (`label` "flag" or "!flag")
  kSets,       // a rule's set_flag action sets a flag
};

struct GraphEdge {
  GraphEdgeKind kind = GraphEdgeKind::kStarts;
  size_t from = 0, to = 0;  // indices into the nodes
  std::string label;
};

struct EventGraph {
  std::vector<GraphNode> nodes;
  std::vector<GraphEdge> edges;

  // The node of a kind and id, or -1.
  [[nodiscard]] int Find(GraphNodeKind kind, const std::string& id) const;
  [[nodiscard]] int Columns() const;
};

// Builds the graph of `doc`: a node for every trigger, every rule and every
// flag or timer a rule mentions, laid out in columns by how far each is from
// something that starts a chain (a loop of flags is cut where it closes).
EventGraph BuildEventGraph(const LevelDoc& doc);

// Wires `from` (a trigger, timer or flag node) into the rule `rule_id` as its
// starting event: a trigger gives the event it emits (enter, exit, stay or
// use); a flag becomes "set to true". Returns false for
// a source that is a rule, or no such node or rule.
bool Connect(LevelDoc& doc, GraphNodeKind kind, const std::string& id,
             const std::string& rule_id);
}

#endif
