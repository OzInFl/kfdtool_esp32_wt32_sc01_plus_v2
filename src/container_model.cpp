#include "container_model.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <vector>

#include <ArduinoJson.h>

static const char* CONTAINERS_PATH = "/containers.json";

ContainerModel& ContainerModel::instance() {
    static ContainerModel inst;
    return inst;
}

ContainerModel::ContainerModel()
    : activeIndex_(-1),
      storageReady_(false)
{
    load();
}

bool ContainerModel::ensureStorage() {
    if (storageReady_) return true;

    // Prefer SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("[ContainerModel] SPIFFS mount failed");
        // You could also attempt SD here if you want:
        // if (!SD.begin()) { ... }
        return false;
    }

    storageReady_ = true;
    return true;
}

void ContainerModel::initDefaults() {
    containers_.clear();

    // Example seed containers
    KeyContainer c1;
    c1.id      = 1;
    c1.label   = "BSO Patrol";
    c1.agency  = "Broward SO";
    c1.algo    = "AES256";
    c1.band    = "700/800";
    c1.hasKeys = false;
    c1.locked  = false;

    KeyContainer c2;
    c2.id      = 2;
    c2.label   = "Plantation FD";
    c2.agency  = "Plantation FD";
    c2.algo    = "AES256";
    c2.band    = "700/800";
    c2.hasKeys = false;
    c2.locked  = false;

    containers_.push_back(c1);
    containers_.push_back(c2);

    activeIndex_ = 0;
}

void ContainerModel::normalizeFlags() {
    for (auto& c : containers_) {
        c.hasKeys = !c.keys.empty();
    }
}

bool ContainerModel::load() {
    if (!ensureStorage()) {
        initDefaults();
        return false;
    }

    if (!SPIFFS.exists(CONTAINERS_PATH)) {
        Serial.println("[ContainerModel] containers.json not found, seeding defaults");
        initDefaults();
        save();
        return true;
    }

    File f = SPIFFS.open(CONTAINERS_PATH, "r");
    if (!f) {
        Serial.println("[ContainerModel] Failed to open containers.json");
        initDefaults();
        return false;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[ContainerModel] JSON parse error: %s\n", err.c_str());
        initDefaults();
        return false;
    }

    containers_.clear();

    JsonArray arr = doc["containers"].as<JsonArray>();
    for (JsonVariant v : arr) {
        KeyContainer kc;
        kc.id      = v["id"]      | 0;
        kc.label   = v["label"]   | "";
        kc.agency  = v["agency"]  | "";
        kc.algo    = v["algo"]    | "AES256";
        kc.band    = v["band"]    | "";
        kc.locked  = v["locked"]  | false;
        kc.hasKeys = false;

        kc.keys.clear();
        JsonArray keys = v["keys"].as<JsonArray>();
        for (JsonVariant kv : keys) {
            KeyEntry ke;
            ke.slot     = kv["slot"]     | 0;
            ke.label    = kv["label"]    | "";
            ke.algo     = kv["algo"]     | "AES256";
            ke.keyHex   = kv["keyHex"]   | "";
            ke.selected = kv["selected"] | false;
            kc.keys.push_back(ke);
        }

        kc.hasKeys = !kc.keys.empty();
        containers_.push_back(kc);
    }

    activeIndex_ = doc["activeIndex"] | -1;
    if (activeIndex_ < 0 || activeIndex_ >= (int)containers_.size()) {
        activeIndex_ = containers_.empty() ? -1 : 0;
    }

    normalizeFlags();
    Serial.printf("[ContainerModel] Loaded %u containers, active=%d\n",
                  (unsigned)containers_.size(), activeIndex_);

    return true;
}

bool ContainerModel::save() {
    if (!ensureStorage()) return false;

    DynamicJsonDocument doc(8192);

    JsonArray arr = doc.createNestedArray("containers");
    for (const auto& kc : containers_) {
        JsonObject o = arr.createNestedObject();
        o["id"]      = kc.id;
        o["label"]   = kc.label;
        o["agency"]  = kc.agency;
        o["algo"]    = kc.algo;
        o["band"]    = kc.band;
        o["locked"]  = kc.locked;

        JsonArray keys = o.createNestedArray("keys");
        for (const auto& ke : kc.keys) {
            JsonObject kv = keys.createNestedObject();
            kv["slot"]     = ke.slot;
            kv["label"]    = ke.label;
            kv["algo"]     = ke.algo;
            kv["keyHex"]   = ke.keyHex;
            kv["selected"] = ke.selected;
        }
    }

    doc["activeIndex"] = activeIndex_;

    File f = SPIFFS.open(CONTAINERS_PATH, "w");
    if (!f) {
        Serial.println("[ContainerModel] Failed to open containers.json for write");
        return false;
    }

    if (serializeJson(doc, f) == 0) {
        Serial.println("[ContainerModel] Failed to serialize JSON");
        f.close();
        return false;
    }

    f.close();
    Serial.println("[ContainerModel] Saved containers.json");
    return true;
}

size_t ContainerModel::getCount() const {
    return containers_.size();
}

const KeyContainer& ContainerModel::get(size_t index) const {
    if (index >= containers_.size()) {
        static KeyContainer dummy;
        return dummy;
    }
    return containers_[index];
}

KeyContainer& ContainerModel::getMutable(size_t index) {
    if (index >= containers_.size()) {
        static KeyContainer dummy;
        return dummy;
    }
    return containers_[index];
}

const KeyContainer* ContainerModel::getActive() const {
    if (activeIndex_ < 0 || activeIndex_ >= (int)containers_.size()) return nullptr;
    return &containers_[activeIndex_];
}

int ContainerModel::getActiveIndex() const {
    return activeIndex_;
}

void ContainerModel::setActiveIndex(int idx) {
    if (idx < 0 || idx >= (int)containers_.size()) {
        activeIndex_ = -1;
    } else {
        activeIndex_ = idx;
    }
    save();
}

int ContainerModel::addContainer(const std::string& label,
                                 const std::string& agency,
                                 const std::string& band,
                                 const std::string& algo)
{
    if (!ensureStorage()) return -1;

    KeyContainer kc;
    kc.id      = static_cast<uint8_t>(containers_.size() + 1);
    kc.label   = label;
    kc.agency  = agency;
    kc.band    = band;
    kc.algo    = algo;
    kc.hasKeys = false;
    kc.locked  = false;
    kc.keys.clear();

    containers_.push_back(kc);

    if (activeIndex_ < 0) {
        activeIndex_ = (int)containers_.size() - 1;
    }

    save();
    return (int)containers_.size() - 1;
}

bool ContainerModel::removeContainer(size_t index) {
    if (index >= containers_.size()) return false;
    if (!ensureStorage()) return false;

    containers_.erase(containers_.begin() + index);

    if (containers_.empty()) {
        activeIndex_ = -1;
    } else if (activeIndex_ >= (int)containers_.size()) {
        activeIndex_ = (int)containers_.size() - 1;
    }

    return save();
}
