#pragma once

#include <vector>
#include <string>
#include <stdint.h>

// Single key entry inside a container
struct KeyEntry {
    uint8_t     slot;      // slot number (1..N)
    std::string label;     // human-readable label
    std::string algo;      // "AES256", "AES128", "DES-OFB", etc.
    std::string keyHex;    // hex string
    bool        selected;  // selected for keyload
};

// Container of keys
struct KeyContainer {
    uint8_t                  id;       // container ID
    std::string              label;    // e.g. "BSO Patrol"
    std::string              agency;   // e.g. "Broward SO"
    std::string              algo;     // default algo for new keys
    std::string              band;     // "700/800", "VHF", etc.
    bool                     hasKeys;  // true if keys.size() > 0
    bool                     locked;   // view-only if true
    std::vector<KeyEntry>    keys;     // keys in this container
};

// Persistent model for all containers
class ContainerModel {
public:
    static ContainerModel& instance();

    // Load from SPIFFS/SD JSON file
    bool load();

    // Save to SPIFFS/SD JSON file
    bool save();

    // Number of containers
    size_t getCount() const;

    // Access containers
    const KeyContainer& get(size_t index) const;
    KeyContainer&       getMutable(size_t index);

    // Active container
    const KeyContainer* getActive() const;
    int                 getActiveIndex() const;
    void                setActiveIndex(int idx);

    // CRUD
    int  addContainer(const std::string& label,
                      const std::string& agency,
                      const std::string& band,
                      const std::string& algo);

    bool removeContainer(size_t index);

private:
    ContainerModel();
    ~ContainerModel() = default;

    bool ensureStorage();
    void initDefaults();
    void normalizeFlags();

private:
    std::vector<KeyContainer> containers_;
    int                       activeIndex_;
    bool                      storageReady_;
};
