#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>

typedef void (*BleCommandCallback)(String payload);

class BleManager {
public:
    void begin();
    void update(); // Reinicia advertising automaticamente após desconexão
    void setOnCommand(BleCommandCallback cb);
    bool isConnected();

private:
    BleCommandCallback _onCommand = nullptr;
};

#endif