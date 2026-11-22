#ifndef MYTHOS_ENGINE_CORE_CVAR_H_
#define MYTHOS_ENGINE_CORE_CVAR_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Console variables and commands: named values that point at a game's own
// variables, read and written as text by name, and a command line that runs
// them. Pure logic; the debug console (ui/debug_console.h) is its front end.
// Names are matched without regard to case.
namespace mythos {
// What a command or a set produced: `ok` and a message to show (may span lines,
// separated by '\n').
struct CvarResult {
  bool ok = true;
  std::string text;
};

class CvarRegistry {
 public:
  // The arguments of a command, not counting its name.
  using Args = std::vector<std::string>;
  using Command = std::function<CvarResult(const Args&)>;

  // Registers `help`, `list` and `find`.
  CvarRegistry();

  // Bind a variable. The pointer must outlive the registry (or be removed with
  // `Remove`). A name must be non-empty with no whitespace or quote; adding
  // one that exists replaces it. All return false for a bad name.
  bool AddBool(const std::string& name, bool* value,
               const std::string& help = "");

  // An int clamped into [lo, hi] on every set.
  bool AddInt(const std::string& name, int* value, int lo, int hi,
              const std::string& help = "");

  // A double clamped into [lo, hi] on every set.
  bool AddDouble(const std::string& name, double* value, double lo, double hi,
                 const std::string& help = "");

  // A variable that lives elsewhere: `get` returns its text and `set` takes
  // new text and reports what came of it.
  bool AddCustom(const std::string& name, std::function<std::string()> get,
                 std::function<CvarResult(const std::string&)> set,
                 const std::string& help = "");

  // A command, run as "name arg arg" (arguments may be "quoted").
  bool AddCommand(const std::string& name, Command run,
                  const std::string& help = "");

  // Removes a variable or command; false when it is not there.
  bool Remove(const std::string& name);

  [[nodiscard]] bool Has(const std::string& name) const;

  // The value as text; empty for a command or an unknown name.
  [[nodiscard]] std::string Get(const std::string& name) const;

  // Sets a variable from text. Fails for an unknown name, a command, or a value
  // that does not parse; the message says which.
  CvarResult Set(const std::string& name, const std::string& value);

  // Runs a line: "name" shows a variable's value, "name value" sets it, a
  // command runs with its arguments. A blank line does nothing. Unknown names
  // fail with a message.
  CvarResult Execute(const std::string& line);

  // Names (as registered) starting with `prefix`, sorted.
  [[nodiscard]] std::vector<std::string> Complete(
      const std::string& prefix) const;

  // Every name, sorted.
  [[nodiscard]] std::vector<std::string> Names() const;

  [[nodiscard]] int Count() const { return static_cast<int>(entries_.size()); }

  // The help text of a name ("" when none or unknown).
  [[nodiscard]] std::string Help(const std::string& name) const;

  // Counts every successful change of a variable, so a game can tell that it
  // must apply its settings again.
  [[nodiscard]] uint64_t Revision() const { return revision_; }

 private:
  struct Entry {
    std::string name;  // as registered
    std::string help;
    std::function<std::string()> get;
    std::function<CvarResult(const std::string&)> set;
    Command command;
  };

  bool Add(const std::string& name, Entry entry);
  const Entry* Find(const std::string& name) const;

  std::map<std::string, Entry> entries_;  // by lower-case name
  uint64_t revision_ = 0;
};

// Splits a command line into words on whitespace; "double quotes" keep spaces
// inside one word. An unclosed quote runs to the end of the line.
std::vector<std::string> SplitCommandLine(const std::string& line);
}  // namespace mythos

#endif
