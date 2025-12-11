#include "key_container.h"

#include <FS.h>
#include <LittleFS.h>

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include <cstring>
#include <vector>

using std::string;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool KeyContainerManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[KeyContainerManager] LittleFS mount failed");
        return false;
    }
    return loadContainers();
}

// Scan root of LittleFS for *.kfc files and load them into _containers.
bool KeyContainerManager::loadContainers() {
    _containers.clear();

    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[KeyContainerManager] LittleFS root missing or not dir");
        return false;
    }

    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String name = f.name();
            if (name.endsWith(".kfc")) {
                KeyContainer kc;
                // For now, fixed demo passphrase; UI can prompt later.
                if (loadFromFile(name.c_str(), "demo-passphrase", kc)) {
                    _containers.push_back(kc);
                } else {
                    Serial.printf("[KeyContainerManager] Failed to load '%s'\n",
                                  name.c_str());
                }
            }
        }
        f = root.openNextFile();
    }

    Serial.printf("[KeyContainerManager] Loaded %u containers from LittleFS\n",
                  (unsigned)_containers.size());
    return true;
}

size_t KeyContainerManager::getCount() const {
    return _containers.size();
}

const KeyContainer* KeyContainerManager::getContainer(size_t idx) const {
    if (idx >= _containers.size()) return nullptr;
    return &_containers[idx];
}

// ---------------------------------------------------------------------------
// AES-256 helpers
// ---------------------------------------------------------------------------

bool KeyContainerManager::aes256Encrypt(const std::vector<uint8_t>& plaintext,
                                        const std::vector<uint8_t>& key,
                                        std::vector<uint8_t>& iv,
                                        std::vector<uint8_t>& ciphertext) {
    if (key.size() != 32) {
        Serial.println("[KeyContainerManager] aes256Encrypt: key size != 32");
        return false;
    }

    iv.resize(16);
    for (int i = 0; i < 16; ++i) {
        iv[i] = (uint8_t)random(0, 256);
    }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key.data(), 256) != 0) {
        Serial.println("[KeyContainerManager] aes256Encrypt: setkey_enc failed");
        mbedtls_aes_free(&ctx);
        return false;
    }

    // PKCS7-style padding: round up to 16 bytes
    size_t paddedLen = ((plaintext.size() + 15) / 16) * 16;
    std::vector<uint8_t> padded(paddedLen, 0);
    memcpy(padded.data(), plaintext.data(), plaintext.size());

    ciphertext.resize(paddedLen);
    uint8_t ivLocal[16];
    memcpy(ivLocal, iv.data(), 16);

    if (mbedtls_aes_crypt_cbc(&ctx,
                              MBEDTLS_AES_ENCRYPT,
                              paddedLen,
                              ivLocal,
                              padded.data(),
                              ciphertext.data()) != 0) {
        Serial.println("[KeyContainerManager] aes256Encrypt: crypt_cbc failed");
        mbedtls_aes_free(&ctx);
        return false;
    }

    mbedtls_aes_free(&ctx);
    return true;
}

bool KeyContainerManager::aes256Decrypt(const std::vector<uint8_t>& ciphertext,
                                        const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& iv,
                                        std::vector<uint8_t>& plaintext) {
    if (key.size() != 32) {
        Serial.println("[KeyContainerManager] aes256Decrypt: key size != 32");
        return false;
    }
    if (ciphertext.empty() || (ciphertext.size() % 16) != 0) {
        Serial.println("[KeyContainerManager] aes256Decrypt: invalid length");
        return false;
    }
    if (iv.size() != 16) {
        Serial.println("[KeyContainerManager] aes256Decrypt: iv size != 16");
        return false;
    }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key.data(), 256) != 0) {
        Serial.println("[KeyContainerManager] aes256Decrypt: setkey_enc failed");
        mbedtls_aes_free(&ctx);
        return false;
    }

    plaintext.resize(ciphertext.size());
    uint8_t ivLocal[16];
    memcpy(ivLocal, iv.data(), 16);

    if (mbedtls_aes_crypt_cbc(&ctx,
                              MBEDTLS_AES_DECRYPT,
                              ciphertext.size(),
                              ivLocal,
                              ciphertext.data(),
                              plaintext.data()) != 0) {
        Serial.println("[KeyContainerManager] aes256Decrypt: crypt_cbc failed");
        mbedtls_aes_free(&ctx);
        return false;
    }

    mbedtls_aes_free(&ctx);
    return true;
}

// ---------------------------------------------------------------------------
// Simple key derivation (SHA-256 based, no PBKDF2)
// ---------------------------------------------------------------------------

// This is intentionally simple to avoid relying on mbedtls_pkcs5_pbkdf2_hmac,
// which is not always exposed in the ESP32 Arduino build.
//
// We compute:
//   key0 = SHA256(passphrase || salt)
//   for i in 1..iterations-1: key = SHA256(key || salt)
//
// and return the final 32-byte key.
std::vector<uint8_t> KeyContainerManager::deriveKeyFromPass(const std::string& passphrase) {
    const char* salt = "KFD-DEMO-SALT";
    const int iterations = 10000;  // still non-trivial, but not insane for ESP32

    std::vector<uint8_t> key(32, 0);

    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        Serial.println("[KeyContainerManager] deriveKeyFromPass: no SHA256 info");
        return key;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, info, 0) != 0) {
        Serial.println("[KeyContainerManager] deriveKeyFromPass: md_setup failed");
        mbedtls_md_free(&ctx);
        return key;
    }

    // Initial hash of passphrase + salt
    if (mbedtls_md_starts(&ctx) != 0 ||
        mbedtls_md_update(&ctx,
                          (const unsigned char*)passphrase.data(),
                          passphrase.size()) != 0 ||
        mbedtls_md_update(&ctx,
                          (const unsigned char*)salt,
                          strlen(salt)) != 0 ||
        mbedtls_md_finish(&ctx, key.data()) != 0) {
        Serial.println("[KeyContainerManager] deriveKeyFromPass: initial hash failed");
        mbedtls_md_free(&ctx);
        return key;
    }

    // Iteratively rehash key + salt
    for (int i = 1; i < iterations; ++i) {
        if (mbedtls_md_starts(&ctx) != 0 ||
            mbedtls_md_update(&ctx, key.data(), key.size()) != 0 ||
            mbedtls_md_update(&ctx,
                              (const unsigned char*)salt,
                              strlen(salt)) != 0 ||
            mbedtls_md_finish(&ctx, key.data()) != 0) {
            Serial.println("[KeyContainerManager] deriveKeyFromPass: loop hash failed");
            break;
        }
    }

    mbedtls_md_free(&ctx);
    return key;
}

// ---------------------------------------------------------------------------
// Container file format and load/save
// ---------------------------------------------------------------------------
//
// Very simple binary format (inside the AES-256 blob):
//
//   [u8 nameLen][name bytes]
//   [u8 descLen][desc bytes]
//   [u8 keyCount]
//   repeat keyCount times:
//       [u16 keysetId]
//       [u16 keyId]
//       [u8  algoId]
//       [u8  keyLen]
//       [keyLen keyBytes]
//
// The encrypted file structure on disk is:
//
//   [16-byte IV][AES-256-CBC ciphertext of the blob above]
//
// ---------------------------------------------------------------------------

bool KeyContainerManager::loadFromFile(const char* path,
                                       const std::string& passphrase,
                                       KeyContainer& out) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[KeyContainerManager] Could not open '%s' for read\n", path);
        return false;
    }

    std::vector<uint8_t> fileData;
    fileData.reserve(f.size());
    while (f.available()) {
        fileData.push_back((uint8_t)f.read());
    }
    f.close();

    if (fileData.size() < 16) {
        Serial.printf("[KeyContainerManager] '%s' too small for IV\n", path);
        return false;
    }

    std::vector<uint8_t> iv(16);
    memcpy(iv.data(), fileData.data(), 16);

    std::vector<uint8_t> ciphertext(fileData.size() - 16);
    memcpy(ciphertext.data(), fileData.data() + 16, ciphertext.size());

    auto key = deriveKeyFromPass(passphrase);
    std::vector<uint8_t> plaintext;
    if (!aes256Decrypt(ciphertext, key, iv, plaintext)) {
        Serial.printf("[KeyContainerManager] AES decrypt failed for '%s'\n", path);
        return false;
    }

    size_t offset = 0;

    if (offset >= plaintext.size()) return false;
    uint8_t nameLen = plaintext[offset++];
    if (offset + nameLen > plaintext.size()) return false;
    out.name.assign((const char*)&plaintext[offset], nameLen);
    offset += nameLen;

    if (offset >= plaintext.size()) return false;
    uint8_t descLen = plaintext[offset++];
    if (offset + descLen > plaintext.size()) return false;
    out.description.assign((const char*)&plaintext[offset], descLen);
    offset += descLen;

    if (offset >= plaintext.size()) return false;
    uint8_t count = plaintext[offset++];

    out.keys.clear();
    out.keys.reserve(count);

    for (uint8_t i = 0; i < count; ++i) {
        if (offset + 2 + 2 + 1 + 1 > plaintext.size()) {
            Serial.println("[KeyContainerManager] loadFromFile: truncated key header");
            return false;
        }

        KeyEntry ke;
        ke.keysetId    = (uint16_t)((plaintext[offset] << 8) | plaintext[offset + 1]);
        offset += 2;
        ke.keyId       = (uint16_t)((plaintext[offset] << 8) | plaintext[offset + 1]);
        offset += 2;
        ke.algorithmId = plaintext[offset++];

        uint8_t keyLen = plaintext[offset++];
        if (offset + keyLen > plaintext.size()) {
            Serial.println("[KeyContainerManager] loadFromFile: truncated key data");
            return false;
        }

        ke.keyData.assign(&plaintext[offset], &plaintext[offset + keyLen]);
        offset += keyLen;

        out.keys.push_back(ke);
    }

    return true;
}

bool KeyContainerManager::saveToFile(const char* path,
                                     const std::string& passphrase,
                                     const KeyContainer& in) {
    std::vector<uint8_t> plaintext;

    uint8_t nameLen = (uint8_t)in.name.size();
    plaintext.push_back(nameLen);
    plaintext.insert(plaintext.end(),
                     (const uint8_t*)in.name.data(),
                     (const uint8_t*)in.name.data() + in.name.size());

    uint8_t descLen = (uint8_t)in.description.size();
    plaintext.push_back(descLen);
    plaintext.insert(plaintext.end(),
                     (const uint8_t*)in.description.data(),
                     (const uint8_t*)in.description.data() + in.description.size());

    uint8_t count = (uint8_t)in.keys.size();
    plaintext.push_back(count);

    for (const auto& e : in.keys) {
        plaintext.push_back((uint8_t)(e.keysetId >> 8));
        plaintext.push_back((uint8_t)(e.keysetId & 0xFF));
        plaintext.push_back((uint8_t)(e.keyId >> 8));
        plaintext.push_back((uint8_t)(e.keyId & 0xFF));
        plaintext.push_back(e.algorithmId);

        uint8_t keyLen = (uint8_t)e.keyData.size();
        plaintext.push_back(keyLen);
        plaintext.insert(plaintext.end(), e.keyData.begin(), e.keyData.end());
    }

    auto key = deriveKeyFromPass(passphrase);

    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    if (!aes256Encrypt(plaintext, key, iv, ciphertext)) {
        Serial.printf("[KeyContainerManager] AES encrypt failed for '%s'\n", path);
        return false;
    }

    std::vector<uint8_t> fileData;
    fileData.reserve(iv.size() + ciphertext.size());
    fileData.insert(fileData.end(), iv.begin(), iv.end());
    fileData.insert(fileData.end(), ciphertext.begin(), ciphertext.end());

    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("[KeyContainerManager] Could not open '%s' for write\n", path);
        return false;
    }

    size_t written = f.write(fileData.data(), fileData.size());
    f.close();

    if (written != fileData.size()) {
        Serial.printf("[KeyContainerManager] Short write on '%s'\n", path);
        return false;
    }

    return true;
}

void KeyContainerManager::loop() {
    // Reserved for periodic tasks (e.g., wiping temp buffers).
}
