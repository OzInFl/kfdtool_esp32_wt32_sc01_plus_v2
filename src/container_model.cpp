#include "container_model.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

static const char* CONTAINER_FILE = "/containers.json";

ContainerModel& ContainerModel::instance() {
    static ContainerModel inst;
    return inst;
}

ContainerModel::ContainerModel()
: _activeIndex(-1)
{
    // Try to load from SPIFFS; if it fails, seed defaults and persist them
    if (!load()) {
        Serial.println("[ContainerModel] No valid SPIFFS containers.json, seeding defaults...");
        seedDefaults();
        save();
    } else {
        Serial.printf("[ContainerModel] Loaded %u containers from SPIFFS\n",
                      (unsigned)_containers.size());
    }
}

bool ContainerModel::ensureStorage() {
    if (_storageInit) return _storageOK;

    _storageInit = true;

    if (!SPIFFS.begin(true)) {
        Serial.println("[ContainerModel] SPIFFS mount failed");
        _storageOK = false;
    } else {
        Serial.println("[ContainerModel] SPIFFS mounted");
        _storageOK = true;
    }

    return _storageOK;
}

void ContainerModel::seedDefaults() {
    _containers.clear();

    {
        KeyContainer kc;
        kc.id      = 1;
        kc.label   = "BSO - Patrol AES256";
        kc.agency  = "Broward SO";
        kc.algo    = "AES256";
        kc.band    = "700/800";
        kc.hasKeys = true;
        kc.locked  = false;

        // Example keys (placeholder hex)
        kc.keys.push_back({ 1, "PATROL ENC 1", "AES256",
                            "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF",
                            true });

        kc.keys.push_back({ 2, "PATROL ENC 2", "AES256",
                            "FFEEDDCCBBAA99887766554433221100FFEEDDCCBBAA99887766554433221100",
                            false });

        _containers.push_back(kc);
    }

    {
        KeyContainer kc;
        kc.id      = 2;
        kc.label   = "Plantation FD - OPS AES256";
        kc.agency  = "Plantation FD";
        kc.algo    = "AES256";
        kc.band    = "700/800";
        kc.hasKeys = true;
        kc.locked  = false;

        kc.keys.push_back({ 1, "FD OPS 1", "AES256",
                            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                            true });

        _containers.push_back(kc);
    }

    {
        KeyContainer kc;
        kc.id      = 3;
        kc.label   = "MGPD - Secure DES-OFB";
        kc.agency  = "Miami Gardens PD";
        kc.algo    = "DES-OFB";
        kc.band    = "UHF";
        kc.hasKeys = true;
        kc.locked  = false;

        kc.keys.push_back({ 1, "SECURE 1", "DES-OFB",
                            "0123456789ABCDEF",
                            true });

        _containers.push_back(kc);
    }

    {
        KeyContainer kc;
        kc.id      = 4;
        kc.label   = "Test / Training";
        kc.agency  = "Lab";
        kc.algo    = "AES256";
        kc.band    = "Multi-band";
        kc.hasKeys = false;
        kc.locked  = false;
        // start with no keys
        _containers.push_back(kc);
    }

    _activeIndex = -1;
}

bool ContainerModel::load() {
    if (!ensureStorage()) return false;

    if (!SPIFFS.exists(CONTAINER_FILE)) {
        Serial.println("[ContainerModel] containers.json not found");
        return false;
    }

    File f = SPIFFS.open(CONTAINER_FILE, "r");
    if (!f) {
        Serial.println("[ContainerModel] Failed to open containers.json");
        return false;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[ContainerModel] JSON parse error: %s\n", err.c_str());
        return false;
    }

    if (!doc.is<JsonArray>()) {
        Serial.println("[ContainerModel] containers.json is not an array");
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();

    std::vector<KeyContainer> tmp;
    tmp.reserve(arr.size());

    for (JsonObject obj : arr) {
        KeyContainer kc;
        kc.id      = obj["id"]      | 0;
        kc.label   = obj["label"]   | "";
        kc.agency  = obj["agency"]  | "";
        kc.algo    = obj["algo"]    | "";
        kc.band    = obj["band"]    | "";
        kc.hasKeys = obj["hasKeys"] | false;
        kc.locked  = obj["locked"]  | false;

        if (kc.label.empty()) continue; // basic sanity

        kc.keys.clear();
        if (obj.containsKey("keys") && obj["keys"].is<JsonArray>()) {
            JsonArray karr = obj["keys"].as<JsonArray>();
            for (JsonObject ko : karr) {
                KeyEntry ke;
                ke.slot     = ko["slot"]     | 0;
                ke.label    = ko["label"]    | "";
                ke.algo     = ko["algo"]     | kc.algo;
                ke.keyHex   = ko["keyHex"]   | "";
                ke.selected = ko["selected"] | false;

                if (!ke.label.empty() && !ke.keyHex.empty()) {
                    kc.keys.push_back(ke);
                }
            }
        }

        kc.hasKeys = !kc.keys.empty();
        tmp.push_back(kc);
    }

    if (tmp.empty()) {
        Serial.println("[ContainerModel] containers.json had no valid entries");
        return false;
    }

    _containers.swap(tmp);
    _activeIndex = -1;
    return true;
}

bool ContainerModel::save() {
    if (!ensureStorage()) return false;

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& kc : _containers) {
        JsonObject obj = arr.createNestedObject();
        obj["id"]      = kc.id;
        obj["label"]   = kc.label;
        obj["agency"]  = kc.agency;
        obj["algo"]    = kc.algo;
        obj["band"]    = kc.band;
        obj["hasKeys"] = kc.hasKeys;
        obj["locked"]  = kc.locked;

        JsonArray karr = obj.createNestedArray("keys");
        for (const auto& ke : kc.keys) {
            JsonObject ko = karr.createNestedObject();
            ko["slot"]     = ke.slot;
            ko["label"]    = ke.label;
            ko["algo"]     = ke.algo;
            ko["keyHex"]   = ke.keyHex;
            ko["selected"] = ke.selected;
        }
    }

    File f = SPIFFS.open(CONTAINER_FILE, "w");
    if (!f) {
        Serial.println("[ContainerModel] Failed to open containers.json for write");
        return false;
    }

    if (serializeJsonPretty(doc, f) == 0) {
        Serial.println("[ContainerModel] Failed to write JSON");
        f.close();
        return false;
    }

    f.close();
    Serial.printf("[ContainerModel] Saved %u containers to %s\n",
                  (unsigned)_containers.size(), CONTAINER_FILE);
    return true;
}

void ContainerModel::resetToFactory() {
    seedDefaults();
    save();
}

size_t ContainerModel::getCount() const {
    return _containers.size();
}

const KeyContainer& ContainerModel::get(size_t idx) const {
    return _containers.at(idx);
}

KeyContainer& ContainerModel::getMutable(size_t idx) {
    return _containers.at(idx);
}

void ContainerModel::setActiveIndex(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= _containers.size()) {
        _activeIndex = -1;
    } else {
        _activeIndex = idx;
    }
}

int ContainerModel::getActiveIndex() const {
    return _activeIndex;
}

const KeyContainer* ContainerModel::getActive() const {
    if (_activeIndex < 0 || static_cast<size_t>(_activeIndex) >= _containers.size()) {
        return nullptr;
    }
    return &_containers[_activeIndex];
}
