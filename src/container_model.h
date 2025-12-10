#pragma once

#include <stdint.h>
#include <vector>
#include <string>

// One key inside a container
struct KeyEntry {
    uint8_t     slot;      // key slot/index (0..N-1)
    std::string label;     // "Patrol ENC", "FD CMD", etc.
    std::string algo;      // "AES256", "AES128", "DES-OFB"
    std::string keyHex;    // hex string, e.g. 64 chars for AES256
    bool        selected;  // selected for load
};

// Container of keys (agency/profile)
struct KeyContainer {
    uint8_t                  id;      // internal ID / slot
    std::string              label;   // "BSO - Patrol AES256"
    std::string              agency;  // "Broward SO"
    std::string              algo;    // default algo if not per-key
    std::string              band;    // "700/800", "VHF"
    bool                     hasKeys; // container has any live keys
    bool                     locked;  // admin locked container
    std::vector<KeyEntry>    keys;    // list of keys
};

class ContainerModel {
public:
    static ContainerModel& instance();

    size_t getCount() const;
    const KeyContainer& get(size_t idx) const;
    KeyContainer&       getMutable(size_t idx);  // for UI edits

    void   setActiveIndex(int idx);
    int    getActiveIndex() const;
    const KeyContainer* getActive() const;

    // Persistence API
    bool load();        // load from SPIFFS (if present)
    bool save();        // save to SPIFFS
    void resetToFactory(); // reset to built-in defaults + save

private:
    ContainerModel();

    void seedDefaults();       // fill _containers with built-in defaults
    bool ensureStorage();      // mount SPIFFS once

    std::vector<KeyContainer> _containers;
    int                       _activeIndex;

    bool _storageInit = false;
    bool _storageOK   = false;
};
