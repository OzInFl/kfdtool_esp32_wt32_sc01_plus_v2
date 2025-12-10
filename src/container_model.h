#pragma once

#include <vector>
#include <string>
#include <stdint.h>

// One key entry inside a container
struct KeySlot {
    std::string label;     // e.g. "TG 1 - Patrol"
    std::string algo;      // e.g. "AES256"
    std::string hex;       // raw key material as hex string (hex-encoded)
    bool        selected;  // whether to include in keyload
};

// A logical key container (like a KVL "keyset")
struct KeyContainer {
    std::string          label;    // "Broward SO - Patrol (AES256)"
    std::string          agency;   // "Broward SO"
    std::string          band;     // "700/800", "VHF", etc.
    std::string          algo;     // default algo label for this container
    bool                 locked;   // true = requires ADMIN to modify
    std::vector<KeySlot> keys;     // keys in this container
};

/**
 * ContainerModel
 *
 * - Singleton model holding all KeyContainer objects in memory
 * - Provides CRUD for containers and keys
 * - Persists to SPIFFS as a simple text file
 */
class ContainerModel {
public:
    static ContainerModel& instance();

    // ----- basic access -----
    size_t            getCount() const;
    const KeyContainer& get(size_t idx) const;
    KeyContainer&     getMutable(size_t idx);

    int               getActiveIndex() const;
    const KeyContainer* getActive() const;
    void              setActiveIndex(int idx);

    // ----- container CRUD -----
    // returns new index or -1 on failure
    int  addContainer(const KeyContainer& kc);
    bool updateContainer(size_t idx, const KeyContainer& kc);
    bool removeContainer(size_t idx);

    // ----- key CRUD inside a container -----
    // returns key index or -1 on failure
    int  addKey(size_t containerIdx, const KeySlot& key);
    bool updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& key);
    bool removeKey(size_t containerIdx, size_t keyIdx);

    // ----- persistence -----
    // Load from SPIFFS; if file missing or invalid, build sane defaults
    bool load();

    // Save to SPIFFS (called automatically by mutating ops)
    bool save();

    // Rebuild in-memory defaults (demo container/key)
    void loadDefaults();

private:
    ContainerModel();
    ContainerModel(const ContainerModel&) = delete;
    ContainerModel& operator=(const ContainerModel&) = delete;

    bool ensureStorage();   // mount SPIFFS if needed
    bool loadFromSPIFFS();
    bool saveToSPIFFS();

    std::vector<KeyContainer> containers_;
    int                       active_index_;

    bool storageReady_;
};

