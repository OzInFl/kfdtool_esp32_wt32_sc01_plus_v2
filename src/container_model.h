#pragma once

#include <vector>
#include <string>
#include <stdint.h>

// UI-level key slot inside a container.
struct KeySlot {
    std::string label;     // e.g. "TG 1 - Patrol"
    std::string algo;      // e.g. "AES256"
    std::string hex;       // raw key material as hex string
    bool        selected;  // whether to include in keyload
};

// UI-level key container (what the operator sees/edits).
struct KeyContainer {
    std::string label;     // user-facing name
    std::string agency;    // "Plantation FD"
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
    bool load();      // Load from LittleFS; if file missing or invalid, build sane defaults.
    bool save();      // Non-blocking: mark state as dirty & remember change time.
    bool saveNow();   // Blocking: immediately write to LittleFS.
    bool factoryReset();
    void loadDefaults();
    void service();   // periodic deferred autosave

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

    // IMPORTANT: returns the new index on success, or -1 on failure.
    int  addContainer(const KeyContainer& c);

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
    bool     dirty_;
    uint32_t last_change_ms_;
    uint32_t last_save_ms_;
};
