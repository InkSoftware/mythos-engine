#include "engine/core/cvar.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace mythos {
namespace {
std::string Lower(const std::string& s) {
  std::string out = s;
  std::ranges::transform(out, out.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool ValidName(const std::string& name) {
  if (name.empty()) return false;
  return std::ranges::none_of(name, [](const unsigned char c) {
    return std::isspace(c) || c == '"' || std::iscntrl(c);
  });
}

bool ParseBool(const std::string& text, bool& out) {
  const std::string t = Lower(text);
  if (t == "1" || t == "true" || t == "on" || t == "yes") {
    out = true;
    return true;
  }
  if (t == "0" || t == "false" || t == "off" || t == "no") {
    out = false;
    return true;
  }
  return false;
}

bool ParseInt(const std::string& text, int& out) {
  long long v = 0;
  const char* const end = text.data() + text.size();
  const auto r = std::from_chars(text.data(), end, v);
  if (r.ec != std::errc() || r.ptr != end) return false;
  out = static_cast<int>(std::clamp<long long>(v, -2000000000, 2000000000));
  return true;
}

bool ParseDouble(const std::string& text, double& out) {
  if (text.empty()) return false;
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  double v = 0;
  in >> v;
  if (in.fail() || !in.eof() || !std::isfinite(v)) return false;
  out = v;
  return true;
}

std::string FormatDouble(const double v) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(6);
  out << v;
  return out.str();
}

CvarResult Fail(std::string text) {
  return {.ok = false, .text = std::move(text)};
}
}  // namespace

std::vector<std::string> SplitCommandLine(const std::string& line) {
  std::vector<std::string> words;
  std::string cur;
  bool in_quote = false, have = false;
  for (const char c : line) {
    if (c == '"') {
      in_quote = !in_quote;
      have = true;
    } else if (!in_quote && std::isspace(static_cast<unsigned char>(c))) {
      if (have) words.push_back(cur);
      cur.clear();
      have = false;
    } else {
      cur += c;
      have = true;
    }
  }
  if (have) words.push_back(cur);
  return words;
}

CvarRegistry::CvarRegistry() {
  AddCommand(
      "help",
      [this](const Args& args) -> CvarResult {
        if (args.empty()) {
          return {.ok = true,
                  .text =
                      "name shows a value, name value sets it. help <name>"
                      ", list [prefix], find <text>"};
        }
        if (!Has(args[0])) return Fail("unknown: " + args[0]);
        const std::string h = Help(args[0]);
        return {.ok = true, .text = h.empty() ? "(no help)" : h};
      },
      "help [name]: how the console works, or what one name is");
  AddCommand(
      "list",
      [this](const Args& args) -> CvarResult {
        std::string out;
        for (const std::string& n : Complete(args.empty() ? "" : args[0])) {
          const std::string v = Get(n);
          if (!out.empty()) out += '\n';
          out += n;
          if (!v.empty()) out += " = " + v;
        }
        return {.ok = true, .text = out.empty() ? "(none)" : out};
      },
      "list [prefix]: every name with its value");
  AddCommand(
      "find",
      [this](const Args& args) -> CvarResult {
        if (args.empty()) return Fail("find <text>");
        const std::string needle = Lower(args[0]);
        std::string out;
        for (const std::string& n : Names()) {
          if (Lower(n).find(needle) == std::string::npos) continue;
          if (!out.empty()) out += '\n';
          out += n;
        }
        return {.ok = true, .text = out.empty() ? "(none)" : out};
      },
      "find <text>: names that contain the text");
}

bool CvarRegistry::Add(const std::string& name, Entry entry) {
  if (!ValidName(name)) return false;
  entry.name = name;
  entries_[Lower(name)] = std::move(entry);
  return true;
}

const CvarRegistry::Entry* CvarRegistry::Find(const std::string& name) const {
  const auto it = entries_.find(Lower(name));
  return it == entries_.end() ? nullptr : &it->second;
}

bool CvarRegistry::AddBool(const std::string& name, bool* const value,
                           const std::string& help) {
  if (!value) return false;
  Entry e;
  e.help = help;
  e.get = [value] { return std::string(*value ? "1" : "0"); };
  e.set = [value](const std::string& text) -> CvarResult {
    bool v = false;
    if (!ParseBool(text, v)) return Fail("expected 0, 1, on or off");
    *value = v;
    return {.ok = true, .text = ""};
  };
  return Add(name, std::move(e));
}

bool CvarRegistry::AddInt(const std::string& name, int* const value,
                          const int lo, const int hi, const std::string& help) {
  if (!value || lo > hi) return false;
  Entry e;
  e.help = help;
  e.get = [value] { return std::to_string(*value); };
  e.set = [value, lo, hi](const std::string& text) -> CvarResult {
    int v = 0;
    if (!ParseInt(text, v)) return Fail("expected a whole number");
    *value = std::clamp(v, lo, hi);
    return {.ok = true, .text = ""};
  };
  return Add(name, std::move(e));
}

bool CvarRegistry::AddDouble(const std::string& name, double* const value,
                             const double lo, const double hi,
                             const std::string& help) {
  if (!value || !(lo <= hi)) return false;
  Entry e;
  e.help = help;
  e.get = [value] { return FormatDouble(*value); };
  e.set = [value, lo, hi](const std::string& text) -> CvarResult {
    double v = 0;
    if (!ParseDouble(text, v)) return Fail("expected a number");
    *value = std::clamp(v, lo, hi);
    return {.ok = true, .text = ""};
  };
  return Add(name, std::move(e));
}

bool CvarRegistry::AddCustom(const std::string& name,
                             std::function<std::string()> get,
                             std::function<CvarResult(const std::string&)> set,
                             const std::string& help) {
  if (!get || !set) return false;
  Entry e;
  e.help = help;
  e.get = std::move(get);
  e.set = std::move(set);
  return Add(name, std::move(e));
}

bool CvarRegistry::AddCommand(const std::string& name, Command run,
                              const std::string& help) {
  if (!run) return false;
  Entry e;
  e.help = help;
  e.command = std::move(run);
  return Add(name, std::move(e));
}

bool CvarRegistry::Remove(const std::string& name) {
  return entries_.erase(Lower(name)) > 0;
}

bool CvarRegistry::Has(const std::string& name) const {
  return Find(name) != nullptr;
}

std::string CvarRegistry::Get(const std::string& name) const {
  const Entry* e = Find(name);
  return e && e->get ? e->get() : std::string();
}

CvarResult CvarRegistry::Set(const std::string& name,
                             const std::string& value) {
  const Entry* e = Find(name);
  if (!e) return Fail("unknown: " + name);
  if (!e->set) return Fail(e->name + " is a command");
  CvarResult r = e->set(value);
  if (r.ok) {
    ++revision_;
    if (r.text.empty()) r.text = e->name + " = " + e->get();
  }
  return r;
}

CvarResult CvarRegistry::Execute(const std::string& line) {
  const std::vector<std::string> words = SplitCommandLine(line);
  if (words.empty()) return {.ok = true, .text = ""};
  const Entry* e = Find(words[0]);
  if (!e) return Fail("unknown: " + words[0]);
  const Args args(words.begin() + 1, words.end());
  if (e->command) return e->command(args);
  if (args.empty()) return {.ok = true, .text = e->name + " = " + e->get()};
  if (args.size() > 1)
    return Fail("one value expected (use quotes for spaces)");
  return Set(words[0], args[0]);
}

std::vector<std::string> CvarRegistry::Complete(
    const std::string& prefix) const {
  const std::string p = Lower(prefix);
  std::vector<std::string> out;
  for (const auto& [key, e] : entries_) {
    if (key.starts_with(p)) out.push_back(e.name);
  }
  return out;
}

std::vector<std::string> CvarRegistry::Names() const { return Complete(""); }

std::string CvarRegistry::Help(const std::string& name) const {
  const Entry* e = Find(name);
  return e ? e->help : std::string();
}
}  // namespace mythos
