#pragma once

#include <stddef.h>
#include <stdint.h>

#include "container_model.h"

// High-level P25 keyload protocol wrapper using UI-level KeyContainer.
// Low-level 3-wire details live in kfd_protocol.cpp.

class KFDProtocol {
public:
    // Initialise GPIO / timers / whatever hardware is used for 3WI/TWI.
    bool begin();

    // Drive background state machine (call from loop() if you want).
    void loop();

    // Start a keyload session from the given UI container.
    bool beginKeyload(const KeyContainer& kc);

private:
    // Internal state machine
    enum State {
        IDLE = 0,
        SESSION_START,
        SENDING_KEYS,
        SESSION_END,
        ERROR
    };

    State        _state         = IDLE;
    KeyContainer _activeContainer;
    size_t       _currentKeyIndex = 0;

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
