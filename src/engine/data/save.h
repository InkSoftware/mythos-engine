// Versioned save files: named JSON sections, per-version migrations, numbered
// slots and atomic writes. Headless; what goes in a section is the game's (see
// world/save_state.h for the engine's own pieces).
#ifndef MYTHOS_ENGINE_DATA_SAVE_H_
#define MYTHOS_ENGINE_DATA_SAVE_H_

#include <functional>
#include <string>
#include <vector>

#include "engine/core/json.h"

namespace mythos {
// A save: a format version and an object of named sections.
struct SaveData {
  int version = 1;
  json::Value sections = json::Value::object();

  // The section `name`, or nullptr.
  [[nodiscard]] const json::Value* Section(const std::string& name) const {
    return json::Find(sections, name);
  }
  void Set(const std::string& name, json::Value value) {
    sections[name] = std::move(value);
  }
};

enum class SaveStatus {
  kOk,
  // No file (or an empty one).
  kMissing,
  // Not JSON, or not a save.
  kCorrupt,
  // Written by a newer version than this build knows.
  kTooNew,
  // A migration failed.
  kMigrationFailed,
  // The file could not be written.
  kWriteFailed,
};

// Upgrades `sections` written at version `from` to `from + 1`. Returns false
// if it cannot. migrations[i] upgrades version i + 1.
using SaveMigration = std::function<bool(json::Value& sections)>;

// The text of a save file.
std::string SaveToText(const SaveData& data);

// Parses `text`. A file older than `current_version` goes through
// `migrations` in order and comes out at `current_version`. `out` is touched
// only on kOk.
SaveStatus SaveFromText(const std::string& text, int current_version,
                        const std::vector<SaveMigration>& migrations,
                        SaveData& out);

// Numbered save slots in a folder: `<dir>/slot<N>.json`.
class SaveSlots {
 public:
  SaveSlots(std::string dir, int current_version,
            std::vector<SaveMigration> migrations = {})
      : dir_(std::move(dir)),
        current_version_(current_version),
        migrations_(std::move(migrations)) {}

  [[nodiscard]] std::string Path(int slot) const;
  [[nodiscard]] bool Exists(int slot) const;

  // Writes `data` at the current version, creating the folder. The old file
  // stays intact if the write fails.
  SaveStatus Save(int slot, SaveData data) const;
  SaveStatus Load(int slot, SaveData& out) const;
  // Returns whether a file was removed.
  bool Delete(int slot) const;

 private:
  std::string dir_;
  int current_version_;
  std::vector<SaveMigration> migrations_;
};
}

#endif
