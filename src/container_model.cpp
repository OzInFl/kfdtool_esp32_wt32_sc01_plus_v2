#include "container_model.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#if KFD_USE_SD
  #include <SD.h>
#endif

// Single file used for all container data
static const char* KFD_CONTAINER_FILE = "/containers.dat";

// --- helpers to access FS without pulling FS type into header ---

static fs::FS& getFS_from_ptr(void* fsPtr) {
    // ugly but effective: we know at call site whether it's SPIFFS or SD
    return *reinterpret_cast<fs::FS*>(fsPtr);
}

// ---------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------

ContainerModel& ContainerModel::instance() {
    static ContainerModel m;
    return m;
}

ContainerModel::ContainerModel()
: active_index_(-1),
  storageReady_(false),
  spiffsMounted_(false),
  sdMounted_(false)
{
    // In-memory defaults until load() is called.
    loadDefaults();
}

// ---------------------------------------------------------------------
// Public API – basic accessors
// ---------------------------------------------------------------------

size_t ContainerModel::getCount() const {
    return containers_.size();
}

const KeyContainer& ContainerModel::get(size_t idx) const {
    return containers_.at(idx);
}

KeyContainer& ContainerModel::getMutable(size_t idx) {
    return containers_.at(idx);
}

int ContainerModel::getActiveIndex() const {
    return active_index_;
}

const KeyContainer* ContainerModel::getActive() const {
    if (active_index_ < 0 || static_cast<size_t>(active_index_) >= containers_.size()) {
        return nullptr;
    }
    return &containers_[active_index_];
}

void ContainerModel::setActiveIndex(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= containers_.size()) {
        active_index_ = -1;
    } else {
        active_index_ = idx;
    }
    // we persist the active index along with containers
    save();
}

// ---------------------------------------------------------------------
// Public API – container CRUD
// ---------------------------------------------------------------------

int ContainerModel::addContainer(const KeyContainer& kc) {
    containers_.push_back(kc);
    int idx = static_cast<int>(containers_.size() - 1);

    // If nothing active, make this one active.
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
    } else {
        // keep active index sane
        if (active_index_ >= static_cast<int>(containers_.size())) {
            active_index_ = static_cast<int>(containers_.size()) - 1;
        }
    }

    save();
    return true;
}

// ---------------------------------------------------------------------
// Public API – key CRUD
// ---------------------------------------------------------------------

int ContainerModel::addKey(size_t containerIdx, const KeySlot& key) {
    if (containerIdx >= containers_.size()) return -1;
    KeyContainer& kc = containers_[containerIdx];
    kc.keys.push_back(key);
    int keyIdx = static_cast<int>(kc.keys.size() - 1);
    save();
    return keyIdx;
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

// ---------------------------------------------------------------------
// Defaults (used when no file on SPIFFS/SD)
// ---------------------------------------------------------------------

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
}

// ---------------------------------------------------------------------
// Persistence – load/save entrypoints
// ---------------------------------------------------------------------

bool ContainerModel::load() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] Storage not ready, using defaults in RAM");
        loadDefaults();
        return false;
    }

    bool ok = false;

#if KFD_USE_SD
    if (sdMounted_) {
        ok = loadFromFS((void*)&SD, true);
    }
#endif
    if (!ok && spiffsMounted_) {
        ok = loadFromFS((void*)&SPIFFS, false);
    }

    if (!ok) {
        Serial.println("[ContainerModel] No container file; building defaults");
        loadDefaults();
        // Save defaults so next boot sees something
        save();
    }

    return ok;
}

bool ContainerModel::save() {
    if (!ensureStorage()) {
        Serial.println("[ContainerModel] Storage not ready, cannot save");
        return false;
    }

    bool ok = false;

#if KFD_USE_SD
    if (sdMounted_) {
        ok = saveToFS((void*)&SD, true);
    }
#endif
    if (!ok && spiffsMounted_) {
        ok = saveToFS((void*)&SPIFFS, false);
    }

    if (!ok) {
        Serial.println("[ContainerModel] Failed to save containers.dat");
    }

    return ok;
}

// ---------------------------------------------------------------------
// Persistence – mount FS
// ---------------------------------------------------------------------

bool ContainerModel::ensureStorage() {
    if (storageReady_) return true;

    // SPIFFS first
    if (!spiffsMounted_) {
        if (!SPIFFS.begin(true)) {
            Serial.println("[ContainerModel] SPIFFS.begin() failed");
        } else {
            spiffsMounted_ = true;
        }
    }

#if KFD_USE_SD
    if (!sdMounted_) {
        if (!SD.begin()) {
            Serial.println("[ContainerModel] SD.begin() failed");
        } else {
            sdMounted_ = true;
        }
    }
#endif

    storageReady_ = spiffsMounted_ || sdMounted_;
    return storageReady_;
}

// ---------------------------------------------------------------------
// Persistence – simple text format encoder/decoder
//
// File format (line-based):
//   HDR|KFDv1|<active_index>
//   C|label|agency|band|algo|locked
//   K|label|algo|hex|selected
//   K|...
//   C|...
//
// Fields are '|' separated. We escape '|' and '\n' in strings as \| and \n.
// ---------------------------------------------------------------------

static String escapeField(const std::string& s) {
    String out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '|' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n' || c == '\r') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

static std::string unescapeField(const String& s) {
    std::string out;
    out.reserve(s.length());
    bool esc = false;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (!esc) {
            if (c == '\\') {
                esc = true;
            } else {
                out.push_back(c);
            }
        } else {
            if (c == 'n') {
                out.push_back('\n');
            } else {
                out.push_back(c);
            }
            esc = false;
        }
    }
    return out;
}

static std::vector<String> splitLine(const String& line) {
    std::vector<String> parts;
    String current;
    bool esc = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (!esc) {
            if (c == '\\') {
                esc = true;
            } else if (c == '|') {
                parts.push_back(current);
                current = "";
            } else {
                current += c;
            }
        } else {
            // part of escape
            current += c;
            esc = false;
        }
    }
    parts.push_back(current);
    return parts;
}

bool ContainerModel::loadFromFS(void* fsPtr, bool useSD) {
    fs::FS& fs = getFS_from_ptr(fsPtr);

    if (!fs.exists(KFD_CONTAINER_FILE)) {
        Serial.printf("[ContainerModel] %s: %s does not exist\n",
                      useSD ? "SD" : "SPIFFS", KFD_CONTAINER_FILE);
        return false;
    }

    File f = fs.open(KFD_CONTAINER_FILE, "r");
    if (!f) {
        Serial.printf("[ContainerModel] %s: open(%s) failed\n",
                      useSD ? "SD" : "SPIFFS", KFD_CONTAINER_FILE);
        return false;
    }

    containers_.clear();
    active_index_ = -1;

    int fileActiveIndex = -1;
    KeyContainer current;
    bool haveCurrent = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        auto parts = splitLine(line);
        if (parts.empty()) continue;

        if (parts[0] == "HDR") {
            if (parts.size() >= 3) {
                fileActiveIndex = parts[2].toInt();
            }
        } else if (parts[0] == "C") {
            // flush previous container
            if (haveCurrent) {
                containers_.push_back(current);
                current = KeyContainer{};
            }
            haveCurrent = true;

            if (parts.size() >= 6) {
                current.label  = unescapeField(parts[1]);
                current.agency = unescapeField(parts[2]);
                current.band   = unescapeField(parts[3]);
                current.algo   = unescapeField(parts[4]);
                current.locked = (parts[5].toInt() != 0);
            }
        } else if (parts[0] == "K") {
            if (!haveCurrent) {
                // malformed, ignore
                continue;
            }
            if (parts.size() >= 5) {
                KeySlot k;
                k.label    = unescapeField(parts[1]);
                k.algo     = unescapeField(parts[2]);
                k.hex      = unescapeField(parts[3]);
                k.selected = (parts[4].toInt() != 0);
                current.keys.push_back(k);
            }
        }
    }

    if (haveCurrent) {
        containers_.push_back(current);
    }

    f.close();

    if (containers_.empty()) {
        Serial.println("[ContainerModel] File had no containers, using defaults");
        loadDefaults();
        return false;
    }

    if (fileActiveIndex >= 0 &&
        static_cast<size_t>(fileActiveIndex) < containers_.size()) {
        active_index_ = fileActiveIndex;
    } else {
        active_index_ = 0;
    }

    Serial.printf("[ContainerModel] Loaded %u containers from %s\n",
                  (unsigned)containers_.size(),
                  useSD ? "SD" : "SPIFFS");
    return true;
}

bool ContainerModel::saveToFS(void* fsPtr, bool useSD) {
    fs::FS& fs = getFS_from_ptr(fsPtr);

    // write atomically via temp file
    const char* tmpFile = "/containers.tmp";

    fs.remove(tmpFile);
    File f = fs.open(tmpFile, "w");
    if (!f) {
        Serial.printf("[ContainerModel] %s: open(%s) for write failed\n",
                      useSD ? "SD" : "SPIFFS", tmpFile);
        return false;
    }

    // header
    f.printf("HDR|KFDv1|%d\n", active_index_);

    for (size_t i = 0; i < containers_.size(); ++i) {
        const KeyContainer& c = containers_[i];

        String lineC = "C|";
        lineC += escapeField(c.label);
        lineC += "|";
        lineC += escapeField(c.agency);
        lineC += "|";
        lineC += escapeField(c.band);
        lineC += "|";
        lineC += escapeField(c.algo);
        lineC += "|";
        lineC += (c.locked ? "1" : "0");
        lineC += "\n";
        f.print(lineC);

        for (size_t kIdx = 0; kIdx < c.keys.size(); ++kIdx) {
            const KeySlot& k = c.keys[kIdx];

            String lineK = "K|";
            lineK += escapeField(k.label);
            lineK += "|";
            lineK += escapeField(k.algo);
            lineK += "|";
            lineK += escapeField(k.hex);
            lineK += "|";
            lineK += (k.selected ? "1" : "0");
            lineK += "\n";
            f.print(lineK);
        }
    }

    f.flush();
    f.close();

    // replace old file
    fs.remove(KFD_CONTAINER_FILE);
    if (!fs.rename(tmpFile, KFD_CONTAINER_FILE)) {
        Serial.printf("[ContainerModel] %s: rename(%s -> %s) failed\n",
                      useSD ? "SD" : "SPIFFS", tmpFile, KFD_CONTAINER_FILE);
        return false;
    }

    Serial.printf("[ContainerModel] Saved %u containers to %s\n",
                  (unsigned)containers_.size(),
                  useSD ? "SD" : "SPIFFS");
    return true;
}
