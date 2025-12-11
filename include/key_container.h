#pragma once

#include <Arduino.h>
#include <vector>
#include <string>
#include <stdint.h>

// A single key entry inside an encrypted key container file.
// This is *not* the same as the UI "KeySlot" in ContainerModel.
struct KeyEntry {
    uint16_t             keysetId;    // logical keyset
    uint16_t             keyId;       // per-key ID
    uint8_t              algorithmId; // e.g. AES, DES, ADP mapping
    std::vector<uint8_t> keyData;     // raw key bytes
};

// A key container file as stored on internal FS (e.g. LittleFS) or SD.
struct KeyContainer {
    std::string           name;        // container name / label
    std::string           description; // optional description
    std::vector<KeyEntry> keys;        // keys in this container

    // Basic sanity check used by higher-level code (e.g. KFDProtocol).
    bool isValid() const {
        // For now, "valid" just means "has at least one key".
        // You can tighten this later (e.g. require non-empty name).
        return !keys.empty();
    }
};

// Manager for on-device key containers stored under LittleFS.
// This is a low-level storage/crypto layer, *separate* from the UI
// "ContainerModel" (which handles per-radio, per-agency metadata).
class KeyContainerManager {
public:
    KeyContainerManager() = default;

    // Mount LittleFS and enumerate *.kfc containers.
    bool begin();

    // Reloads the in-memory list of containers from LittleFS.
    bool loadContainers();

    // Number of loaded containers.
    size_t getCount() const;

    // Read-only access to a container by index.
    const KeyContainer* getContainer(size_t idx) const;

    // Simple load/save helpers for a specific file.
    bool loadFromFile(const char* path,
                      const std::string& passphrase,
                      KeyContainer& out);

    bool saveToFile(const char* path,
                    const std::string& passphrase,
                    const KeyContainer& in);

    // Future use for periodic tasks (e.g., secure erase scheduling).
    void loop();

private:
    // AES-256-CBC encryption/decryption.
    bool aes256Encrypt(const std::vector<uint8_t>& plaintext,
                       const std::vector<uint8_t>& key,
                       std::vector<uint8_t>& iv,
                       std::vector<uint8_t>& ciphertext);

    bool aes256Decrypt(const std::vector<uint8_t>& ciphertext,
                       const std::vector<uint8_t>& key,
                       const std::vector<uint8_t>& iv,
                       std::vector<uint8_t>& plaintext);

    // Simple SHA-256–based key derivation (not full PBKDF2, but sufficient
    // for this embedded demo; can be upgraded later).
    std::vector<uint8_t> deriveKeyFromPass(const std::string& passphrase);

    std::vector<KeyContainer> _containers;
};
