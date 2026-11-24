#include "engine/data/save.h"

#include <filesystem>
#include <utility>

#include "engine/core/fileio.h"

namespace mythos {
std::string SaveToText(const SaveData& data) {
  json::Value root = json::Value::object();
  root["version"] = data.version;
  root["sections"] = data.sections;
  return root.dump(2) + "\n";
}

SaveStatus SaveFromText(const std::string& text, const int current_version,
                        const std::vector<SaveMigration>& migrations,
                        SaveData& out) {
  if (text.empty()) return SaveStatus::kMissing;
  json::Value root;
  if (!json::Parse(text, root) || !root.is_object()) {
    return SaveStatus::kCorrupt;
  }
  const json::Value* version = json::Find(root, "version");
  const json::Value* sections = json::Find(root, "sections");
  if (version == nullptr || !version->is_number_integer() ||
      sections == nullptr || !sections->is_object()) {
    return SaveStatus::kCorrupt;
  }
  int v = version->get<int>();
  if (v < 1) return SaveStatus::kCorrupt;
  if (v > current_version) return SaveStatus::kTooNew;
  json::Value migrated = *sections;
  for (; v < current_version; ++v) {
    const auto index = static_cast<size_t>(v - 1);
    if (index >= migrations.size() || !migrations[index] ||
        !migrations[index](migrated) || !migrated.is_object()) {
      return SaveStatus::kMigrationFailed;
    }
  }
  out.version = current_version;
  out.sections = std::move(migrated);
  return SaveStatus::kOk;
}

std::string SaveSlots::Path(const int slot) const {
  return (std::filesystem::path(dir_) /
          ("slot" + std::to_string(slot) + ".json"))
      .string();
}

bool SaveSlots::Exists(const int slot) const {
  std::error_code ec;
  return std::filesystem::exists(Path(slot), ec);
}

SaveStatus SaveSlots::Save(const int slot, SaveData data) const {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  data.version = current_version_;
  return WriteFileAtomic(Path(slot), SaveToText(data))
             ? SaveStatus::kOk
             : SaveStatus::kWriteFailed;
}

SaveStatus SaveSlots::Load(const int slot, SaveData& out) const {
  return SaveFromText(ReadTextFile(Path(slot)), current_version_, migrations_,
                      out);
}

bool SaveSlots::Delete(const int slot) const {
  std::error_code ec;
  return std::filesystem::remove(Path(slot), ec);
}
}
