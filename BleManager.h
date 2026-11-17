#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>

typedef void (*BleAuthCallback)(bool authorized, const String& userId);

class BleManager {
public:
    void begin();
    void update();
    void setOnAuthResult(BleAuthCallback cb);
    bool isConnected();

private:
    BleAuthCallback _onAuthResult = nullptr;
};

#endif