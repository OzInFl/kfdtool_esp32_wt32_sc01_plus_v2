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

    // NOTE: ui.cpp and kfd_protocol.cpp expect this member to be named "keys".
    std::vector<KeySlot> keys;

    // Basic validity check used by higher-level code (e.g. keyload start).
    bool isValid() const {
        // Must at least have one key with non-empty hex.
        if (keys.empty()) return false;
        for (const auto& k : keys) {
            if (!k.hex.empty()) {
                return true;
            }
        }
        return false;
    }
};

class ContainerModel {
public:
    static ContainerModel& instance();

    // ----- persistence -----
    // Load from LittleFS; if file missing or invalid, build sane defaults
    bool load();

    // Save to LittleFS
    bool save();

    // Erase filesystem and rebuild defaults (used by factory reset)
    bool factoryReset();

    // Rebuild in-memory defaults (demo container/key)
    void loadDefaults();

    // ----- basic CRUD (used by ui.cpp) -----
    size_t getCount() const;

    // Older-style API used by ui.cpp:
    const KeyContainer& get(size_t idx) const;
    KeyContainer&       getMutable(size_t idx);

    // Newer-style pointer API (also available if you want it)
    const KeyContainer* getContainer(size_t idx) const;
    KeyContainer*       getContainer(size_t idx);

    int  getActiveIndex() const;
    bool setActiveIndex(int idx);

    // Convenience: pointer to active container (for keyload screens)
    const KeyContainer* getActive() const;

    bool addContainer(const KeyContainer& c);
    bool updateContainer(size_t idx, const KeyContainer& c);
    bool deleteContainer(size_t idx);
    bool moveContainer(size_t fromIdx, size_t toIdx);

    // Legacy names used by ui.cpp:
    bool removeContainer(size_t idx) { return deleteContainer(idx); }

    // Per-key operations used by ui.cpp:
    bool addKey(size_t containerIdx, const KeySlot& slot);
    bool updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& slot);

private:
    ContainerModel();
    ContainerModel(const ContainerModel&) = delete;
    ContainerModel& operator=(const ContainerModel&) = delete;

    bool ensureStorage();   // mount LittleFS if needed
    bool loadFromSPIFFS();  // internal helpers, use LittleFS underneath
    bool saveToSPIFFS();

    std::vector<KeyContainer> containers_;
    int                       active_index_;

    bool storageReady_;
};
