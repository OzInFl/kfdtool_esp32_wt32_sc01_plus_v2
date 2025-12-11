#include <Arduino.h>
#include "kfd_protocol.h"

// -----------------------------------------------------------------------------
// Pin assignments for the 3-wire interface – adjust to your hardware.
// -----------------------------------------------------------------------------
static constexpr int PIN_TWI_DATA = 21;
static constexpr int PIN_TWI_CLK  = 22;
static constexpr int PIN_TWI_EN   = 23;

// -----------------------------------------------------------------------------
// Low-level helpers (currently stubs / debug only)
// -----------------------------------------------------------------------------

bool KFDProtocol::begin() {
  // Configure GPIO as outputs; you can tune this to your real wiring.
  pinMode(PIN_TWI_DATA, OUTPUT);
  pinMode(PIN_TWI_CLK,  OUTPUT);
  pinMode(PIN_TWI_EN,   OUTPUT);

  digitalWrite(PIN_TWI_DATA, LOW);
  digitalWrite(PIN_TWI_CLK,  LOW);
  digitalWrite(PIN_TWI_EN,   LOW);

  _state           = IDLE;
  _currentKeyIndex = 0;

  Serial.println("[KFD] begin(): interface initialised (stub)");
  return true;
}

void KFDProtocol::loop() {
  stateMachine();
}

void KFDProtocol::twiSetData(bool level)   { digitalWrite(PIN_TWI_DATA, level ? HIGH : LOW); }
void KFDProtocol::twiSetClock(bool level)  { digitalWrite(PIN_TWI_CLK,  level ? HIGH : LOW); }
void KFDProtocol::twiSetEnable(bool level) { digitalWrite(PIN_TWI_EN,   level ? HIGH : LOW); }
bool KFDProtocol::twiGetData()             { return digitalRead(PIN_TWI_DATA) != 0; }

// For now these are just stubs. When you’re ready to implement real
// protocol timing, expand these.
void KFDProtocol::sendBit(bool bit) {
  (void)bit;
  // TODO: implement real timing; for now just a debug placeholder.
  // twiSetData(bit);
  // delayMicroseconds(5);
  // twiSetClock(true);
  // delayMicroseconds(5);
  // twiSetClock(false);
}

void KFDProtocol::sendByte(uint8_t value) {
  for (int i = 7; i >= 0; --i) {
    bool b = (value >> i) & 0x01;
    sendBit(b);
  }
}

void KFDProtocol::sendFrame(const uint8_t* data, size_t len) {
  // Stub: in real implementation you would frame bytes with start/stop
  // conditions and any header the radio expects. For now we just log.
  Serial.print("[KFD] sendFrame: ");
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X", data[i]);
  }
  Serial.println();
}

bool KFDProtocol::recvFrame(uint8_t* buf, size_t maxLen, size_t& outLen) {
  (void)buf;
  (void)maxLen;
  outLen = 0;
  // Stub: no real receive yet.
  return false;
}

// -----------------------------------------------------------------------------
// High-level API
// -----------------------------------------------------------------------------

bool KFDProtocol::beginKeyload(const KeyContainer& kc) {
  if (!kc.isValid()) {
    Serial.println("[KFD] beginKeyload(): container not valid (no keys)");
    return false;
  }

  if (_state != IDLE) {
    Serial.println("[KFD] beginKeyload(): already busy");
    return false;
  }

  _activeContainer   = kc;     // copy UI-level container
  _currentKeyIndex   = 0;
  _state             = SESSION_START;

  Serial.printf("[KFD] beginKeyload(): %u keys queued (label='%s')\n",
                (unsigned)_activeContainer.keys.size(),
                _activeContainer.label.c_str());
  return true;
}

// Convert hex string to bytes (utility for the stub).
static bool hexToBytes(const std::string& hex, uint8_t* out, size_t& outLen, size_t maxLen) {
  outLen = 0;
  size_t n = hex.size();
  if (n == 0) return false;
  if (n % 2 != 0) return false;

  size_t bytes = n / 2;
  if (bytes > maxLen) bytes = maxLen;

  for (size_t i = 0; i < bytes; ++i) {
    char hi = hex[2 * i];
    char lo = hex[2 * i + 1];

    auto cvt = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      return -1;
    };

    int vhi = cvt(hi);
    int vlo = cvt(lo);
    if (vhi < 0 || vlo < 0) return false;

    out[i] = (uint8_t)((vhi << 4) | vlo);
  }

  outLen = bytes;
  return true;
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void KFDProtocol::stateMachine() {
  switch (_state) {
    case IDLE:
      // Nothing to do
      break;

    case SESSION_START: {
      Serial.println("[KFD] SESSION_START");
      // In real life: assert EN, send any session start frames, etc.
      twiSetEnable(true);
      _state = SENDING_KEYS;
      break;
    }

    case SENDING_KEYS: {
      if (_currentKeyIndex >= _activeContainer.keys.size()) {
        _state = SESSION_END;
        break;
      }

      const KeySlot& e = _activeContainer.keys[_currentKeyIndex];

      // Skip keys that are not selected or have no hex data.
      if (!e.selected || e.hex.empty()) {
        Serial.printf("[KFD] Skipping key %u ('%s') – not selected/empty\n",
                      (unsigned)_currentKeyIndex,
                      e.label.c_str());
        _currentKeyIndex++;
        break;
      }

      Serial.printf("[KFD] Sending key %u: label='%s', algo='%s'\n",
                    (unsigned)_currentKeyIndex,
                    e.label.c_str(),
                    e.algo.c_str());

      uint8_t keyBuf[64];
      size_t  keyLen = 0;
      if (!hexToBytes(e.hex, keyBuf, keyLen, sizeof(keyBuf))) {
        Serial.println("[KFD] hexToBytes failed; marking ERROR");
        _state = ERROR;
        break;
      }

      // For now, just send raw key bytes as a frame.
      sendFrame(keyBuf, keyLen);

      // Advance to next key; we send one key per stateMachine() pass.
      _currentKeyIndex++;
      break;
    }

    case SESSION_END: {
      Serial.println("[KFD] SESSION_END");
      // In real life: send session-end frame, drop EN, etc.
      twiSetEnable(false);
      _state           = IDLE;
      _currentKeyIndex = 0;
      break;
    }

    case ERROR: {
      Serial.println("[KFD] ERROR state; aborting session");
      twiSetEnable(false);
      _state           = IDLE;
      _currentKeyIndex = 0;
      break;
    }
  }
}
