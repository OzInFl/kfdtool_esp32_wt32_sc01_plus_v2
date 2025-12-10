#include "kfd_protocol.h"
#include <Arduino.h>

// Pin definitions - adjust to your hardware wiring
static const int PIN_TWI_DATA   = 4;
static const int PIN_TWI_CLOCK  = 5;
static const int PIN_TWI_ENABLE = 6;

bool KFDProtocol::begin() {
  pinMode(PIN_TWI_DATA, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_TWI_CLOCK, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_TWI_ENABLE, OUTPUT);

  twiSetEnable(false);
  twiSetClock(true);
  twiSetData(true);
  return true;
}

void KFDProtocol::loop() {
  stateMachine();
}

bool KFDProtocol::beginKeyload(const KeyContainer& kc) {
  if (!kc.isValid()) return false;
  if (_state != IDLE) return false;

  _activeContainer = kc;
  _currentKeyIndex = 0;
  _state = SESSION_START;
  return true;
}

void KFDProtocol::twiSetData(bool level) {
  digitalWrite(PIN_TWI_DATA, level ? HIGH : LOW);
}

void KFDProtocol::twiSetClock(bool level) {
  digitalWrite(PIN_TWI_CLOCK, level ? HIGH : LOW);
}

void KFDProtocol::twiSetEnable(bool level) {
  digitalWrite(PIN_TWI_ENABLE, level ? HIGH : LOW);
}

bool KFDProtocol::twiGetData() {
  pinMode(PIN_TWI_DATA, INPUT_PULLUP);
  bool v = digitalRead(PIN_TWI_DATA);
  pinMode(PIN_TWI_DATA, OUTPUT_OPEN_DRAIN);
  return v;
}

void KFDProtocol::sendBit(bool bit) {
  twiSetData(bit);
  delayMicroseconds(5);
  twiSetClock(true);
  delayMicroseconds(5);
  twiSetClock(false);
}

void KFDProtocol::sendByte(uint8_t value) {
  for (int i = 7; i >= 0; --i) {
    sendBit((value >> i) & 1);
  }
}

void KFDProtocol::sendFrame(const uint8_t* data, size_t len) {
  // This is intentionally just a placeholder. The actual P25 KFD frame
  // format must follow TIA-102.AACD-A and the implementation in KFDtool.
  for (size_t i = 0; i < len; ++i) {
    sendByte(data[i]);
  }
}

bool KFDProtocol::recvFrame(uint8_t* buf, size_t maxLen, size_t& outLen) {
  // Placeholder: you would implement RX sampling and framing here.
  outLen = 0;
  return false;
}

void KFDProtocol::stateMachine() {
  switch (_state) {
    case IDLE:
      return;

    case SESSION_START: {
      twiSetEnable(true);
      delayMicroseconds(50);
      uint8_t frame[] = {0xAA, 0x55}; // demo sync
      sendFrame(frame, sizeof(frame));
      _state = SENDING_KEYS;
      break;
    }

    case SENDING_KEYS: {
      if (_currentKeyIndex >= _activeContainer.keys.size()) {
        _state = SESSION_END;
        break;
      }

      const auto& e = _activeContainer.keys[_currentKeyIndex];
      uint8_t hdr[6];
      hdr[0] = (e.keysetId >> 8) & 0xFF;
      hdr[1] = (e.keysetId)&0xFF;
      hdr[2] = (e.keyId >> 8) & 0xFF;
      hdr[3] = (e.keyId)&0xFF;
      hdr[4] = e.algorithmId;
      hdr[5] = (uint8_t)e.keyData.size();

      sendFrame(hdr, sizeof(hdr));
      if (!e.keyData.empty()) {
        sendFrame(e.keyData.data(), e.keyData.size());
      }

      _currentKeyIndex++;
      // In a real implementation, wait for ACK/NAK frame here.
      break;
    }

    case SESSION_END: {
      uint8_t frame[] = {0x55, 0xAA}; // demo end marker
      sendFrame(frame, sizeof(frame));
      delayMicroseconds(50);
      twiSetEnable(false);
      _state = IDLE;
      break;
    }

    case ERROR: {
      twiSetEnable(false);
      _state = IDLE;
      break;
    }
  }
}
