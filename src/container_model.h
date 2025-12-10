#pragma once

#include <vector>
#include <string>
#include <stdint.h>

struct KeySlot {
    std::string label;     // e.g. "TG 1 - Patrol"
    std::string algo;      // e.g. "AES256"
    std::string hex;       // raw key material as hex string
    bool        selected;  // whether to include in keyload
};

struct KeyContainer {
    std::string          label;    // "Broward SO - Patrol (AES256)"
    std::string          agency;   // "Broward SO"
    std::string          band;     // "700/800", "VHF" etc.
    std::string          algo;     // default algo for this container
    bool                 locked;   // locked / read-only flag
    std::vector<KeySlot> keys;     // keys in this container
};

// Set to 1 via build_flags if you want SD card as a secondary backend
#ifndef KFD_USE_SD
#define KFD_USE_SD 0
#endif

class ContainerModel {
public:
    static ContainerModel& instance();

    // ----- basic container list -----
    size_t          getCount() const;
    const KeyContainer& get(size_t idx) const;
    KeyContainer&   getMutable(size_t idx);

    int             getActiveIndex() const;
    const KeyContainer* getActive() const;
    void            setActiveIndex(int idx);

    // ----- container CRUD -----
    // returns new index or -1 on failure
    int  addContainer(const KeyContainer& kc);
    bool updateContainer(size_t idx, const KeyContainer& kc);
    bool removeContainer(size_t idx);

    // ----- key CRUD inside a container -----
    int  addKey(size_t containerIdx, const KeySlot& key);               // returns key index or -1
    bool updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& key);
    bool removeKey(size_t containerIdx, size_t keyIdx);

    // ----- persistence -----
    bool load();   // load from SPIFFS/SD; if not found, build defaults
    bool save();   // save to SPIFFS/SD (called automatically by mutating ops)

    // Rebuild in-memory defaults and overwrite storage on next save()
    void loadDefaults();

private:
    ContainerModel();
    ContainerModel(const ContainerModel&) = delete;
    ContainerModel& operator=(const ContainerModel&) = delete;

    bool ensureStorage();   // mount FS if needed
    bool loadFromFS(void* fsPtr, bool useSD);
    bool saveToFS(void* fsPtr, bool useSD);

    std::vector<KeyContainer> containers_;
    int                       active_index_;

    bool storageReady_;
    bool spiffsMounted_;
    bool sdMounted_;
};
