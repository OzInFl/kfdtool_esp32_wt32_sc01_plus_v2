#include "container_model.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

using std::string;

// Single file used for all container data
static const char* KFD_CONTAINER_FILE = "/containers.dat";

ContainerModel& ContainerModel::instance() {
    static ContainerModel inst;
    return inst;
}

ContainerModel::ContainerModel()
    : active_index_(-1),
      storageReady_(false),
      dirty_(false),
      last_change_ms_(0),
      last_save_ms_(0)
{
    containers_.clear();
}

// -------------------------------------------------------
// Defaults
// -------------------------------------------------------

void ContainerModel::loadDefaults() {
    containers_.clear();
    active_index_ = -1;

    // Example default container(s) – demo values only
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
    k2.hex      = "0123456789ABCDEF0123456789ABCDEF";
    k2.selected = false;
    c1.keys.push_back(k2);

    containers_.push_back(c1);
    active_index_ = 0;

    Serial.printf("[ContainerModel] Defaults loaded (%u containers)\n",
                  (unsigned)containers_.size());

    dirty_ = true;
    last_change_ms_ = millis();
}

// -------------------------------------------------------
// Storage helpers (LittleFS)
// -------------------------------------------------------

bool ContainerModel::ensureStorage() {
    if (storageReady_) return true;

    if (!LittleFS.begin(true)) { // formatOnFail = true
        Serial.println("[ContainerModel] LittleFS.begin() failed");
        return false;
    }

    storageReady_ = true;
    return true;
}

// Simple text format:
//
//   KFDv1 <active_index> <container_count>   (header)
//   C <label>
//   A <agency>
//   B <band>
//   G <algo>
//   L <0/1 locked>
//   K <slot_label>|<algo>|<hex>|<selected 0/1>
//
// We do not strictly rely on <container_count>; we parse until EOF.
//

bool ContainerModel::loadFromSPIFFS() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] loadFromSPIFFS(): storage not ready");
        return false;
    }

    if (!LittleFS.exists(KFD_CONTAINER_FILE)) {
        Serial.println("[ContainerModel] no containers file; using defaults");
        loadDefaults();
        saveToSPIFFS();
        return true;
    }

    File f = LittleFS.open(KFD_CONTAINER_FILE, FILE_READ);
    if (!f) {
        Serial.println("[ContainerModel] open for read failed; using defaults");
        loadDefaults();
        return false;
    }

    // --- header ---
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("KFDv1")) {
        Serial.println("[ContainerModel] invalid header signature; using defaults");
        f.close();
        loadDefaults();
        saveToSPIFFS();
        return false;
    }

    int activeIdx = -1;
    int declaredCount = 0;
    {
        int space1 = line.indexOf(' ');
        int space2 = line.indexOf(' ', space1 + 1);
        if (space1 >= 0 && space2 > space1) {
            activeIdx     = line.substring(space1 + 1, space2).toInt();
            declaredCount = line.substring(space2 + 1).toInt();
        }
    }

    containers_.clear();
    active_index_ = -1;

    KeyContainer current;
    bool inContainer = false;

    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (!l.length()) continue;

        char type = l.charAt(0);

        if (type == 'C') {
            // Start of new container
            if (inContainer) {
                containers_.push_back(current);
            }
            current = KeyContainer();
            inContainer = true;
            current.label = l.substring(2).c_str();
        } else if (!inContainer) {
            continue;
        } else if (type == 'A') {
            current.agency = l.substring(2).c_str();
        } else if (type == 'B') {
            current.band = l.substring(2).c_str();
        } else if (type == 'G') {
            current.algo = l.substring(2).c_str();
        } else if (type == 'L') {
            current.locked = (l.substring(2).toInt() != 0);
        } else if (type == 'K') {
            String rest = l.substring(2);
            // label|algo|hex|selected
            String parts[4];
            int partIdx = 0;

            int start = 0;
            while (partIdx < 4) {
                int p = rest.indexOf('|', start);
                if (p < 0) {
                    parts[partIdx++] = rest.substring(start);
                    break;
                } else {
                    parts[partIdx++] = rest.substring(start, p);
                    start = p + 1;
                }
            }

            KeySlot slot;
            if (partIdx > 0) slot.label = parts[0].c_str();
            if (partIdx > 1) slot.algo  = parts[1].c_str();
            if (partIdx > 2) slot.hex   = parts[2].c_str();
            if (partIdx > 3) slot.selected = (parts[3].toInt() != 0);
            else             slot.selected = false;

            current.keys.push_back(slot);
        }
    }

    if (inContainer) {
        containers_.push_back(current);
    }

    f.close();

    if (containers_.empty()) {
        Serial.println("[ContainerModel] parsed zero containers; using defaults");
        loadDefaults();
        saveToSPIFFS();
        return true;
    }

    if (activeIdx < 0 || activeIdx >= (int)containers_.size()) {
        active_index_ = 0;
    } else {
        active_index_ = activeIdx;
    }

    Serial.printf("[ContainerModel] Loaded %u containers from LittleFS (active=%d, declared=%d)\n",
                  (unsigned)containers_.size(), active_index_, declaredCount);

    dirty_ = false;
    last_save_ms_ = millis();
    return true;
}

bool ContainerModel::saveToSPIFFS() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] saveToSPIFFS(): storage not ready");
        return false;
    }

    File f = LittleFS.open(KFD_CONTAINER_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("[ContainerModel] open for write failed");
        return false;
    }

    int activeIdx = active_index_;
    if (activeIdx < 0 || activeIdx >= (int)containers_.size()) {
        activeIdx = (containers_.empty() ? -1 : 0);
    }

    f.printf("KFDv1 %d %u\n", activeIdx, (unsigned)containers_.size());

    for (const auto& c : containers_) {
        f.printf("C %s\n", c.label.c_str());
        f.printf("A %s\n", c.agency.c_str());
        f.printf("B %s\n", c.band.c_str());
        f.printf("G %s\n", c.algo.c_str());
        f.printf("L %d\n", c.locked ? 1 : 0);

        for (const auto& ks : c.keys) {
            f.printf("K %s|%s|%s|%d\n",
                     ks.label.c_str(),
                     ks.algo.c_str(),
                     ks.hex.c_str(),
                     ks.selected ? 1 : 0);
        }
    }

    f.close();
    Serial.printf("[ContainerModel] Saved %u containers to LittleFS (active=%d)\n",
                  (unsigned)containers_.size(), activeIdx);
    return true;
}

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

bool ContainerModel::load() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] Storage not ready, using defaults in RAM");
        loadDefaults();
        return false;
    }

    if (!loadFromSPIFFS()) {
        return false;
    }

    return true;
}

bool ContainerModel::save() {
    dirty_ = true;
    last_change_ms_ = millis();
    Serial.printf("[ContainerModel] save() -> mark dirty (count=%u)\n",
                  (unsigned)containers_.size());
    return true;
}

bool ContainerModel::saveNow() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] saveNow(): storage not ready");
        return false;
    }
    if (!saveToSPIFFS()) {
        Serial.printf("[ContainerModel] saveNow() failed; RAM-only state (%u containers)\n",
                      (unsigned)containers_.size());
        return false;
    }

    dirty_ = false;
    last_save_ms_ = millis();
    Serial.printf("[ContainerModel] saveNow() OK (%u containers)\n",
                  (unsigned)containers_.size());
    return true;
}

bool ContainerModel::factoryReset() {
    Serial.println("[ContainerModel] FACTORY RESET requested");

    if (!ensureStorage()) {
        Serial.println("[ContainerModel] factoryReset(): storage not ready");
        return false;
    }

    if (!LittleFS.format()) {
        Serial.println("[ContainerModel] factoryReset(): LittleFS.format() failed");
        return false;
    }

    storageReady_ = false;
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] factoryReset(): remount after format failed");
        return false;
    }

    loadDefaults();
    if (!saveToSPIFFS()) {
        Serial.println("[ContainerModel] factoryReset(): save defaults failed");
        return false;
    }

    dirty_ = false;
    last_save_ms_ = millis();

    Serial.println("[ContainerModel] FACTORY RESET complete (defaults written)");
    return true;
}

void ContainerModel::service() {
    if (!dirty_) return;

    uint32_t now = millis();
    const uint32_t MIN_SETTLE_MS   = 1000;
    const uint32_t MIN_INTERVAL_MS = 3000;

    if (now - last_change_ms_ < MIN_SETTLE_MS) return;
    if (now - last_save_ms_   < MIN_INTERVAL_MS) return;

    (void)saveNow();
}

// ----- CRUD -----

size_t ContainerModel::getCount() const {
    return containers_.size();
}

const KeyContainer& ContainerModel::get(size_t idx) const {
    if (idx >= containers_.size()) {
        static KeyContainer dummy;
        return dummy;
    }
    return containers_[idx];
}

KeyContainer& ContainerModel::getMutable(size_t idx) {
    if (idx >= containers_.size()) {
        static KeyContainer dummy;
        return dummy;
    }
    return containers_[idx];
}

const KeyContainer* ContainerModel::getContainer(size_t idx) const {
    if (idx >= containers_.size()) return nullptr;
    return &containers_[idx];
}

KeyContainer* ContainerModel::getContainer(size_t idx) {
    if (idx >= containers_.size()) return nullptr;
    return &containers_[idx];
}

int ContainerModel::getActiveIndex() const {
    return active_index_;
}

bool ContainerModel::setActiveIndex(int idx) {
    if (idx < 0 || idx >= (int)containers_.size()) {
        return false;
    }
    active_index_ = idx;
    save();
    return true;
}

const KeyContainer* ContainerModel::getActive() const {
    if (active_index_ < 0 || active_index_ >= (int)containers_.size()) {
        return nullptr;
    }
    return &containers_[active_index_];
}

int ContainerModel::addContainer(const KeyContainer& c) {
    containers_.push_back(c);
    if (active_index_ < 0) {
        active_index_ = 0;
    }
    save();
    return (int)containers_.size() - 1;
}

bool ContainerModel::updateContainer(size_t idx, const KeyContainer& c) {
    if (idx >= containers_.size()) return false;
    containers_[idx] = c;
    return save();
}

bool ContainerModel::deleteContainer(size_t idx) {
    if (idx >= containers_.size()) return false;
    containers_.erase(containers_.begin() + idx);
    if (containers_.empty()) {
        active_index_ = -1;
    } else if (active_index_ >= (int)containers_.size()) {
        active_index_ = (int)containers_.size() - 1;
    }
    return save();
}

bool ContainerModel::moveContainer(size_t fromIdx, size_t toIdx) {
    if (fromIdx >= containers_.size() || toIdx >= containers_.size()) {
        return false;
    }
    if (fromIdx == toIdx) return true;

    auto tmp = containers_[fromIdx];
    containers_.erase(containers_.begin() + fromIdx);
    containers_.insert(containers_.begin() + toIdx, tmp);

    if (active_index_ == (int)fromIdx) {
        active_index_ = (int)toIdx;
    } else if (active_index_ > (int)fromIdx && active_index_ <= (int)toIdx) {
        active_index_--;
    } else if (active_index_ < (int)fromIdx && active_index_ >= (int)toIdx) {
        active_index_++;
    }

    return save();
}

bool ContainerModel::addKey(size_t containerIdx, const KeySlot& slot) {
    if (containerIdx >= containers_.size()) return false;
    containers_[containerIdx].keys.push_back(slot);
    return save();
}

bool ContainerModel::updateKey(size_t containerIdx, size_t keyIdx, const KeySlot& slot) {
    if (containerIdx >= containers_.size()) return false;
    auto& kc = containers_[containerIdx];
    if (keyIdx >= kc.keys.size()) return false;
    kc.keys[keyIdx] = slot;
    return save();
}

bool ContainerModel::removeKey(size_t containerIdx, size_t keyIdx) {
    if (containerIdx >= containers_.size()) return false;
    auto& kc = containers_[containerIdx];
    if (keyIdx >= kc.keys.size()) return false;
    kc.keys.erase(kc.keys.begin() + keyIdx);
    return save();
}
