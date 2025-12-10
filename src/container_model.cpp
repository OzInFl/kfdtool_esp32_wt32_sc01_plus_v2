#include "container_model.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

using std::string;

// Single file used for all container data
static const char* KFD_CONTAINER_FILE = "/containers.dat";

ContainerModel& ContainerModel::instance() {
    static ContainerModel inst;
    return inst;
}

ContainerModel::ContainerModel()
    : active_index_(-1),
      storageReady_(false) {
}

// -------------------------------------------------------
// Defaults (demo data if nothing is on SPIFFS)
// -------------------------------------------------------

void ContainerModel::loadDefaults() {
    containers_.clear();
    active_index_ = -1;

    // Example default container(s) – safe demo values only
    KeyContainer c1;
    c1.label  = "DEMO - AES256 Patrol";
    c1.agency = "Demo Agency";
    c1.band   = "700/800";
    c1.algo   = "AES256";
    c1.locked = false;

    KeySlot k1;
    k1.label    = "TG 1 - PATROL";
    k1.algo     = "AES256";
    k1.hex      = "00112233445566778899AABBCCDDEEFF";
    k1.selected = true;
    c1.keys.push_back(k1);

    KeySlot k2;
    k2.label    = "TG 2 - TAC";
    k2.algo     = "AES256";
    k2.hex      = "FFEEDDCCBBAA99887766554433221100";
    k2.selected = false;
    c1.keys.push_back(k2);

    containers_.push_back(c1);
    active_index_ = 0;

    Serial.printf("[ContainerModel] Defaults loaded (%u containers)\n",
                  (unsigned)containers_.size());
}

// -------------------------------------------------------
// Storage helpers (SPIFFS only)
// -------------------------------------------------------

bool ContainerModel::ensureStorage() {
    if (storageReady_) return true;

    if (!SPIFFS.begin(true)) { // formatOnFail = true
        Serial.println("[ContainerModel] SPIFFS.begin() failed");
        return false;
    }

    storageReady_ = true;
    return true;
}

// Simple line-based format:
//
//   KFDv1 <active_index> <container_count>
//   C <label>
//   A <agency>
//   B <band>
//   G <algo>
//   L <0/1 locked>
//   N <key_count>
//   (for each key)
//     K <label>
//     g <algo>
//     H <hex>
//     S <0/1 selected>
//
// No newlines in any text fields.
//

bool ContainerModel::loadFromSPIFFS() {
    containers_.clear();
    active_index_ = -1;

    if (!ensureStorage()) {
        Serial.println("[ContainerModel] loadFromSPIFFS(): storage not ready");
        return false;
    }

    if (!SPIFFS.exists(KFD_CONTAINER_FILE)) {
        Serial.println("[ContainerModel] no containers file; using defaults");
        loadDefaults();
        return true;
    }

    File f = SPIFFS.open(KFD_CONTAINER_FILE, FILE_READ);
    if (!f) {
        Serial.println("[ContainerModel] open for read failed; using defaults");
        loadDefaults();
        return false;
    }

    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("KFDv1")) {
        Serial.println("[ContainerModel] invalid header signature; using defaults");
        f.close();
        loadDefaults();
        return false;
    }

    int headerActiveIdx = -1;
    int containerCount  = 0;
    if (sscanf(line.c_str(), "KFDv1 %d %d", &headerActiveIdx, &containerCount) != 2) {
        Serial.println("[ContainerModel] header parse failed; using defaults");
        f.close();
        loadDefaults();
        return false;
    }

    for (int i = 0; i < containerCount; ++i) {
        if (!f.available()) break;

        KeyContainer kc;

        // C <label>
        line = f.readStringUntil('\n'); line.trim();
        if (!line.startsWith("C ")) break;
        kc.label = string(line.c_str() + 2);

        // A <agency>
        line = f.readStringUntil('\n'); line.trim();
        if (!line.startsWith("A ")) break;
        kc.agency = string(line.c_str() + 2);

        // B <band>
        line = f.readStringUntil('\n'); line.trim();
        if (!line.startsWith("B ")) break;
        kc.band = string(line.c_str() + 2);

        // G <algo>
        line = f.readStringUntil('\n'); line.trim();
        if (!line.startsWith("G ")) break;
        kc.algo = string(line.c_str() + 2);

        // L <0/1 locked>
        line = f.readStringUntil('\n'); line.trim();
        int lockedInt = 0;
        if (sscanf(line.c_str(), "L %d", &lockedInt) != 1) break;
        kc.locked = (lockedInt != 0);

        // N <key_count>
        line = f.readStringUntil('\n'); line.trim();
        int keyCount = 0;
        if (sscanf(line.c_str(), "N %d", &keyCount) != 1) break;

        kc.keys.clear();
        for (int k = 0; k < keyCount; ++k) {
            if (!f.available()) break;

            KeySlot ks;

            // K <label>
            line = f.readStringUntil('\n'); line.trim();
            if (!line.startsWith("K ")) break;
            ks.label = string(line.c_str() + 2);

            // g <algo>
            line = f.readStringUntil('\n'); line.trim();
            if (!line.startsWith("g ")) break;
            ks.algo = string(line.c_str() + 2);

            // H <hex>
            line = f.readStringUntil('\n'); line.trim();
            if (!line.startsWith("H ")) break;
            ks.hex = string(line.c_str() + 2);

            // S <0/1 selected>
            line = f.readStringUntil('\n'); line.trim();
            int selInt = 0;
            if (sscanf(line.c_str(), "S %d", &selInt) != 1) break;
            ks.selected = (selInt != 0);

            kc.keys.push_back(ks);
        }

        containers_.push_back(kc);
    }

    f.close();

    if (!containers_.empty()) {
        if (headerActiveIdx >= 0 && headerActiveIdx < (int)containers_.size()) {
            active_index_ = headerActiveIdx;
        } else {
            active_index_ = 0;
        }
    } else {
        active_index_ = -1;
    }

    Serial.printf("[ContainerModel] Loaded %u containers from SPIFFS (active=%d)\n",
                  (unsigned)containers_.size(), active_index_);
    return true;
}

bool ContainerModel::saveToSPIFFS() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] saveToSPIFFS(): storage not ready");
        return false;
    }

    File f = SPIFFS.open(KFD_CONTAINER_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("[ContainerModel] open for write failed");
        return false;
    }

    int activeIdx = active_index_;
    if (activeIdx < 0 || activeIdx >= (int)containers_.size()) {
        activeIdx = -1;
    }

    // Header
    f.printf("KFDv1 %d %u\n", activeIdx, (unsigned)containers_.size());

    // Containers
    for (const auto& kc : containers_) {
        f.print("C ");
        f.println(kc.label.c_str());

        f.print("A ");
        f.println(kc.agency.c_str());

        f.print("B ");
        f.println(kc.band.c_str());

        f.print("G ");
        f.println(kc.algo.c_str());

        f.printf("L %d\n", kc.locked ? 1 : 0);

        f.printf("N %u\n", (unsigned)kc.keys.size());
        for (const auto& ks : kc.keys) {
            f.print("K ");
            f.println(ks.label.c_str());

            f.print("g ");
            f.println(ks.algo.c_str());

            f.print("H ");
            f.println(ks.hex.c_str());

            f.printf("S %d\n", ks.selected ? 1 : 0);
        }
    }

    f.close();
    Serial.printf("[ContainerModel] Saved %u containers to SPIFFS\n",
                  (unsigned)containers_.size());
    return true;
}

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

size_t ContainerModel::getCount() const {
    return containers_.size();
}

const KeyContainer& ContainerModel::get(size_t idx) const {
    static KeyContainer empty;
    if (idx >= containers_.size()) return empty;
    return containers_[idx];
}

KeyContainer& ContainerModel::getMutable(size_t idx) {
    // caller is expected to bounds-check; still guard
    if (idx >= containers_.size()) {
        // fallback: last element if out-of-range
        return containers_.back();
    }
    return containers_[idx];
}

int ContainerModel::getActiveIndex() const {
    return active_index_;
}

const KeyContainer* ContainerModel::getActive() const {
    if (active_index_ < 0 || (size_t)active_index_ >= containers_.size()) {
        return nullptr;
    }
    return &containers_[active_index_];
}

void ContainerModel::setActiveIndex(int idx) {
    if (idx < 0 || (size_t)idx >= containers_.size()) {
        active_index_ = -1;
    } else {
        active_index_ = idx;
    }
    // Persist active index as part of the model
    save();
}

// ----- container CRUD -----

int ContainerModel::addContainer(const KeyContainer& kc) {
    containers_.push_back(kc);
    int idx = (int)containers_.size() - 1;

    if (active_index_ < 0) {
        active_index_ = idx;
    }

    save();
    return idx;
}

bool ContainerModel::updateContainer(size_t idx, const KeyContainer& kc) {
    if (idx >= containers_.size()) return false;
    containers_[idx] = kc;
    save();
    return true;
}

bool ContainerModel::removeContainer(size_t idx) {
    if (idx >= containers_.size()) return false;

    containers_.erase(containers_.begin() + idx);

    if (containers_.empty()) {
        active_index_ = -1;
    } else if (active_index_ >= (int)containers_.size()) {
        active_index_ = (int)containers_.size() - 1;
    }

    save();
    return true;
}

// ----- key CRUD -----

int ContainerModel::addKey(size_t containerIdx, const KeySlot& key) {
    if (containerIdx >= containers_.size()) return -1;
    KeyContainer& kc = containers_[containerIdx];
    kc.keys.push_back(key);
    int idx = (int)kc.keys.size() - 1;
    save();
    return idx;
}

bool ContainerModel::updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& key) {
    if (containerIdx >= containers_.size()) return false;
    KeyContainer& kc = containers_[containerIdx];
    if (keyIdx >= kc.keys.size()) return false;
    kc.keys[keyIdx] = key;
    save();
    return true;
}

bool ContainerModel::removeKey(size_t containerIdx, size_t keyIdx) {
    if (containerIdx >= containers_.size()) return false;
    KeyContainer& kc = containers_[containerIdx];
    if (keyIdx >= kc.keys.size()) return false;
    kc.keys.erase(kc.keys.begin() + keyIdx);
    save();
    return true;
}

// ----- persistence entrypoints -----

bool ContainerModel::load() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] Storage not ready, using defaults in RAM");
        loadDefaults();
        return false;
    }

    if (!loadFromSPIFFS()) {
        // loadFromSPIFFS already falls back to defaults
        return false;
    }

    return true;
}

bool ContainerModel::save() {
    // TEMP: disable actual SPIFFS writes while we stabilize UI/touch.
    // All containers/keys will live in RAM only for this session.
    Serial.printf("[ContainerModel] save() stub (no SPIFFS write) - %u containers\n",
                  (unsigned)containers_.size());
    return true;
}


