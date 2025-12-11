#pragma once

#include <Arduino.h>
#include <vector>
#include <string>
#include <stdint.h>

// This header defines the *on-disk* encrypted key container primitives.
// For now, only a minimal stub is provided so the rest of the project
// (UI, ContainerModel, KFDProtocol) can build cleanly. We can wire real
// encrypted container support back in later.

// A single encrypted key entry on disk (stub for now).
struct KeyEntry {
    uint16_t             keysetId;    // logical keyset
    uint16_t             keyId;       // per-key ID
    uint8_t              algorithmId; // e.g. AES, DES, ADP mapping
    std::vector<uint8_t> keyData;     // raw key bytes
};

// Forward-declared manager for encrypted container files.
// Currently implemented as a no-op stub.
class KeyContainerManager {
public:
    bool begin()          { return true; }
    bool loadContainers() { return true; }

    size_t getCount() const { return 0; }

    // No containers are actually loaded in the stub.
    const void* getContainer(size_t) const { return nullptr; }

    bool loadFromFile(const char*, const std::string&, void*) { return false; }
    bool saveToFile(const char*, const std::string&, const void*) { return false; }

    void loop() {}
};
