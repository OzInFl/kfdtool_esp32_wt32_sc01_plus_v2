#pragma once
#include <vector>
#include <string>

struct KeyEntry {
  uint16_t keysetId;
  uint16_t keyId;
  uint8_t algorithmId;
  std::vector<uint8_t> keyData; // raw key bytes (16 or 32)
};

class KeyContainer {
public:
  std::string name;
  std::string description;
  std::vector<KeyEntry> keys;

  bool isValid() const { return !keys.empty(); }
};

class KeyContainerManager {
public:
  bool begin();              // init storage / crypto
  void loop();               // background tasks

  bool loadContainers();     // scan SPIFFS/SD
  bool addContainer(const KeyContainer& kc);
  size_t getContainerCount() const;
  const KeyContainer* getContainer(size_t index) const;

  bool getSecureMode() const { return _secureMode; }
  void setSecureMode(bool enabled);

private:
  bool _secureMode = true;   // secure by default
  std::vector<KeyContainer> _containers;

  bool loadFromFile(const char* path, const std::string& passphrase, KeyContainer& out);
  bool saveToFile(const char* path, const std::string& passphrase, const KeyContainer& in);

  bool aes256Encrypt(const std::vector<uint8_t>& plaintext,
                     const std::vector<uint8_t>& key,
                     std::vector<uint8_t>& iv,
                     std::vector<uint8_t>& ciphertext);

  bool aes256Decrypt(const std::vector<uint8_t>& ciphertext,
                     const std::vector<uint8_t>& key,
                     const std::vector<uint8_t>& iv,
                     std::vector<uint8_t>& plaintext);

  std::vector<uint8_t> deriveKeyFromPass(const std::string& passphrase);
};
