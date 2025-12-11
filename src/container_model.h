#pragma once

#include <vector>
#include <string>
#include <stdint.h>

// One key entry inside a UI-level container (what the user edits on-screen).
struct KeySlot {
    std::string label;     // e.g. "TG 1 - Patrol"
    std::string algo;      // e.g. "AES256"
    std::string hex;       // raw key material as hex string
    bool        selected;  // whether to include in keyload
};

// A logical key container (like a KVL "keyset") as seen by the UI.
struct KeyContainer {
    std::string label;     // user-facing name
    std::string agency;    // "Plantation FD", etc.
    std::string band;      // "700/800", "VHF", etc.
    std::string algo;      // "AES256", "ADP", "DES-OFB"
    bool        locked;    // true = container locked and cannot be edited

    // NOTE: ui.cpp expects this member to be named "keys".
    std::vector<KeySlot> keys;

    // Basic validity check used by higher-level code (e.g. keyload start).
    bool isValid() const {
        if (keys.empty()) return false;
        for (const auto& k : keys) {
            if (!k.hex.empty()) return true;
        }
        return false;
    }
};

class ContainerModel {
public:
    static ContainerModel& instance();

    // ----- persistence -----

    // Load from LittleFS; if file missing or invalid, build sane defaults.
    bool load();

    // Called by mutating operations (add/update/delete). This is now
    // NON-BLOCKING: it only marks state as "dirty" and records a timestamp.
    bool save();

    // Explicit, blocking save that really writes to LittleFS immediately.
    // Used by the "Save Now" button and by factory reset.
    bool saveNow();

    // Erase LittleFS and rebuild defaults, then save them.
    bool factoryReset();

    // Rebuild in-memory defaults (demo container/key) without touching FS.
    void loadDefaults();

    // Periodic service: call from loop(). If there are pending changes
    // and they've been idle for a bit, this writes them to LittleFS.
    void service();

    // ----- basic access -----

    size_t              getCount() const;
    const KeyContainer& get(size_t idx) const;
    KeyContainer&       getMutable(size_t idx);

    const KeyContainer* getContainer(size_t idx) const;
    KeyContainer*       getContainer(size_t idx);

    int                 getActiveIndex() const;
    bool                setActiveIndex(int idx);
    const KeyContainer* getActive() const;

    // ----- container CRUD -----

    bool addContainer(const KeyContainer& c);
    bool updateContainer(size_t idx, const KeyContainer& c);
    bool deleteContainer(size_t idx);
    bool moveContainer(size_t fromIdx, size_t toIdx);

    // Legacy alias used by ui.cpp
    bool removeContainer(size_t idx) { return deleteContainer(idx); }

    // ----- key CRUD -----

    bool addKey(size_t containerIdx, const KeySlot& slot);
    bool updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& slot);
    bool removeKey(size_t containerIdx, size_t keyIdx);

private:
    ContainerModel();
    ContainerModel(const ContainerModel&) = delete;
    ContainerModel& operator=(const ContainerModel&) = delete;

    bool ensureStorage();   // mount LittleFS if needed
    bool loadFromSPIFFS();  // internal helpers, use LittleFS underneath
    bool saveToSPIFFS();

    std::vector<KeyContainer> containers_;
    int                       active_index_;

    bool     storageReady_;
    bool     dirty_;            // true if RAM state needs to be flushed
    uint32_t last_change_ms_;   // last time save() was called
    uint32_t last_save_ms_;     // last time we actually wrote to flash
};
