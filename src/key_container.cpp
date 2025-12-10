#include "key_container.h"
#include <Arduino.h>
#include <FS.h>
#include "SPIFFS.h"
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <cstring>
#include <vector>

bool KeyContainerManager::begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return false;
  }
  return loadContainers();
}

void KeyContainerManager::loop() {
  // reserved for periodic secure erasure, etc.
}

bool KeyContainerManager::loadContainers() {
  _containers.clear();
  File root = SPIFFS.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("SPIFFS root missing");
    return false;
  }

  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String name = f.name();
      if (name.endsWith(".kfc")) { // KFDtool-inspired key container extension
        KeyContainer kc;
        if (loadFromFile(name.c_str(), "demo-passphrase", kc)) { // TODO: UI passphrase prompt
          _containers.push_back(kc);
        }
      }
    }
    f = root.openNextFile();
  }
  Serial.printf("Loaded %u containers\r\n", (unsigned)_containers.size());
  return true;
}

bool KeyContainerManager::addContainer(const KeyContainer& kc) {
  if (!kc.isValid()) return false;
  _containers.push_back(kc);

  char path[64];
  snprintf(path, sizeof(path), "/kc_%u.kfc", (unsigned)_containers.size());
  if (!saveToFile(path, "demo-passphrase", kc)) {
    Serial.println("Failed to save container");
    return false;
  }
  return true;
}

size_t KeyContainerManager::getContainerCount() const {
  return _containers.size();
}

const KeyContainer* KeyContainerManager::getContainer(size_t index) const {
  if (index >= _containers.size()) return nullptr;
  return &_containers[index];
}

void KeyContainerManager::setSecureMode(bool enabled) {
  _secureMode = enabled;
  // In a real build, toggle extra mitigations, wipe buffers more aggressively, etc.
}

bool KeyContainerManager::loadFromFile(const char* path, const std::string& passphrase, KeyContainer& out) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;

  if (f.size() < 16) return false;

  std::vector<uint8_t> iv(16);
  f.read(iv.data(), iv.size());

  std::vector<uint8_t> ciphertext;
  ciphertext.resize(f.size() - iv.size());
  f.read(ciphertext.data(), ciphertext.size());
  f.close();

  auto key = deriveKeyFromPass(passphrase);

  std::vector<uint8_t> plaintext;
  if (!aes256Decrypt(ciphertext, key, iv, plaintext)) {
    Serial.println("Container decrypt failed");
    return false;
  }

  // Very simple TLV-ish demo format:
  // [count16] [entries...]
  if (plaintext.size() < 2) return false;
  uint16_t count = (plaintext[0] << 8) | plaintext[1];
  size_t offset = 2;

  out.keys.clear();
  for (uint16_t i = 0; i < count; ++i) {
    if (offset + 7 > plaintext.size()) return false;
    KeyEntry e;
    e.keysetId    = (plaintext[offset] << 8) | plaintext[offset + 1];
    e.keyId       = (plaintext[offset + 2] << 8) | plaintext[offset + 3];
    e.algorithmId = plaintext[offset + 4];
    uint8_t len   = plaintext[offset + 5];
    offset += 6;
    if (offset + len > plaintext.size()) return false;
    e.keyData.assign(plaintext.begin() + offset, plaintext.begin() + offset + len);
    offset += len;
    out.keys.push_back(e);
  }

  out.name = path;
  out.description = "Imported container";
  return true;
}

bool KeyContainerManager::saveToFile(const char* path, const std::string& passphrase, const KeyContainer& in) {
  std::vector<uint8_t> plaintext;
  uint16_t count = in.keys.size();
  plaintext.push_back(count >> 8);
  plaintext.push_back(count & 0xFF);

  for (auto& e : in.keys) {
    plaintext.push_back(e.keysetId >> 8);
    plaintext.push_back(e.keysetId & 0xFF);
    plaintext.push_back(e.keyId >> 8);
    plaintext.push_back(e.keyId & 0xFF);
    plaintext.push_back(e.algorithmId);
    plaintext.push_back((uint8_t)e.keyData.size());
    plaintext.insert(plaintext.end(), e.keyData.begin(), e.keyData.end());
  }

  auto key = deriveKeyFromPass(passphrase);

  std::vector<uint8_t> iv(16);
  for (auto &b : iv) {
    b = (uint8_t)random(0, 256);
  }

  std::vector<uint8_t> ciphertext;
  if (!aes256Encrypt(plaintext, key, iv, ciphertext)) return false;

  File f = SPIFFS.open(path, "w");
  if (!f) return false;

  f.write(iv.data(), iv.size());
  f.write(ciphertext.data(), ciphertext.size());
  f.close();
  return true;
}

// Manual PBKDF2-HMAC-SHA256 using mbedTLS HMAC API
std::vector<uint8_t> KeyContainerManager::deriveKeyFromPass(const std::string& passphrase) {
  const char* salt = "KFDtool-ESP32";
  const size_t salt_len = strlen(salt);
  const size_t key_len = 32;
  const uint32_t iterations = 10000;

  std::vector<uint8_t> key(key_len);

  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md_info) {
    Serial.println("mbedtls_md_info_from_type failed");
    return key;
  }

  const size_t hash_len = mbedtls_md_get_size(md_info);

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {
    Serial.println("mbedtls_md_setup failed");
    mbedtls_md_free(&ctx);
    return key;
  }

  std::vector<uint8_t> U(hash_len);
  std::vector<uint8_t> T(hash_len);
  std::vector<uint8_t> salt_block(salt_len + 4);

  memcpy(salt_block.data(), salt, salt_len);

  size_t generated = 0;
  uint32_t block_index = 1;

  while (generated < key_len) {
    // salt || INT_32_BE(block_index)
    salt_block[salt_len + 0] = (block_index >> 24) & 0xFF;
    salt_block[salt_len + 1] = (block_index >> 16) & 0xFF;
    salt_block[salt_len + 2] = (block_index >> 8) & 0xFF;
    salt_block[salt_len + 3] = (block_index) & 0xFF;

    // U1 = HMAC(pass, salt||block_index)
    mbedtls_md_hmac_starts(&ctx,
                           reinterpret_cast<const unsigned char*>(passphrase.data()),
                           passphrase.size());
    mbedtls_md_hmac_update(&ctx, salt_block.data(), salt_block.size());
    mbedtls_md_hmac_finish(&ctx, U.data());

    memcpy(T.data(), U.data(), hash_len);

    // U2..Uiter
    for (uint32_t i = 1; i < iterations; ++i) {
      mbedtls_md_hmac_starts(&ctx,
                             reinterpret_cast<const unsigned char*>(passphrase.data()),
                             passphrase.size());
      mbedtls_md_hmac_update(&ctx, U.data(), hash_len);
      mbedtls_md_hmac_finish(&ctx, U.data());

      for (size_t j = 0; j < hash_len; ++j) {
        T[j] ^= U[j];
      }
    }

    size_t to_copy = (generated + hash_len > key_len) ? (key_len - generated) : hash_len;
    memcpy(key.data() + generated, T.data(), to_copy);
    generated += to_copy;
    block_index++;
  }

  mbedtls_md_free(&ctx);
  return key;
}

bool KeyContainerManager::aes256Encrypt(const std::vector<uint8_t>& plaintext,
                                        const std::vector<uint8_t>& key,
                                        std::vector<uint8_t>& iv,
                                        std::vector<uint8_t>& ciphertext) {
  if (key.size() != 32 || iv.size() != 16) return false;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key.data(), 256);

  // CBC with PKCS7 padding
  size_t blockSize = 16;
  size_t pad = blockSize - (plaintext.size() % blockSize);
  std::vector<uint8_t> padded = plaintext;
  padded.insert(padded.end(), pad, (uint8_t)pad);

  ciphertext.resize(padded.size());
  unsigned char ivCopy[16];
  memcpy(ivCopy, iv.data(), 16);

  mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, padded.size(),
                        ivCopy, padded.data(), ciphertext.data());

  mbedtls_aes_free(&ctx);
  return true;
}

bool KeyContainerManager::aes256Decrypt(const std::vector<uint8_t>& ciphertext,
                                        const std::vector<uint8_t>& key,
                                        const std::vector<uint8_t>& iv,
                                        std::vector<uint8_t>& plaintext) {
  if (key.size() != 32 || iv.size() != 16) return false;
  if (ciphertext.size() % 16 != 0) return false;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, key.data(), 256);

  plaintext.resize(ciphertext.size());
  unsigned char ivCopy[16];
  memcpy(ivCopy, iv.data(), 16);

  mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, ciphertext.size(),
                        ivCopy, ciphertext.data(), plaintext.data());

  mbedtls_aes_free(&ctx);

  if (plaintext.empty()) return false;
  uint8_t pad = plaintext.back();
  if (pad == 0 || pad > 16 || pad > plaintext.size()) return false;
  plaintext.resize(plaintext.size() - pad);
  return true;
}
