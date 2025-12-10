#pragma once

#include "key_container.h"

// Stub of a P25 3-wire interface keyload engine.
// This is intentionally high-level; you'll map the low-level TWI/3WI pins
// and timing based on KFDtool developer notes and the P25 standard.

class KFDProtocol {
public:
  bool begin();   // configure GPIO, timers, etc.
  void loop();    // background state machine

  bool beginKeyload(const KeyContainer& kc);

private:
  enum State {
    IDLE,
    SESSION_START,
    SENDING_KEYS,
    SESSION_END,
    ERROR
  };

  State _state = IDLE;
  size_t _currentKeyIndex = 0;
  KeyContainer _activeContainer;

  // Low-level 3-wire primitives (DATA, CLK, EN)
  void twiSetData(bool level);
  void twiSetClock(bool level);
  void twiSetEnable(bool level);
  bool twiGetData();

  void sendBit(bool bit);
  void sendByte(uint8_t value);
  void sendFrame(const uint8_t* data, size_t len);
  bool recvFrame(uint8_t* buf, size_t maxLen, size_t& outLen);

  void stateMachine();
};
