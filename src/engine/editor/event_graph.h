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

// More edits the graph panel makes on the rules (each returns false for no
// such rule, or an empty name):
// - `AddSetFlag`: the rule gets a `set_flag` action for the flag (once).
// - `AddCondition`: the rule reads the flag (`value` is what it must be; an
//   existing condition on that flag is changed).
// - `RemoveCondition`: drops the conditions on that flag.
// - `DeleteRule`: removes the rule.
// - `AddRuleFrom`: a new rule (the next `rule_N`) started by a trigger, timer
//   or flag, no actions yet; returns its id, or empty for a bad source.
bool AddSetFlag(LevelDoc& doc, const std::string& rule_id,
                const std::string& flag);
bool AddCondition(LevelDoc& doc, const std::string& rule_id,
                  const std::string& flag, bool value = true);
bool RemoveCondition(LevelDoc& doc, const std::string& rule_id,
                     const std::string& flag);
bool DeleteRule(LevelDoc& doc, const std::string& rule_id);
std::string AddRuleFrom(LevelDoc& doc, GraphNodeKind kind,
                        const std::string& id);

// Node positions the player arranged, as text for a file next to the level:
// one `kind id x y` line each (`kind` the `GraphNodeKind` number, `id` a token
// without spaces; x and y in panel pixels). `ParseGraphLayout` skips lines it
// cannot read.
struct GraphPlacement {
  int kind = 0;
  std::string id;
  float x = 0, y = 0;

  bool operator==(const GraphPlacement&) const = default;
};
std::string SerializeGraphLayout(const std::vector<GraphPlacement>& nodes);
std::vector<GraphPlacement> ParseGraphLayout(const std::string& text);
}

#endif
