#ifndef HARDWARE_CONTROL_H
#define HARDWARE_CONTROL_H

#include <Arduino.h>

typedef void (*CardReadCallback)(String uid);

class HardwareControl {
public:
    void begin();
    void update();
    void unlock();
    void showMessage(const String& line1, const String& line2 = "");
    void setOnCardRead(CardReadCallback cb);

private:
    bool _isUnlocked = false;
    bool _displayReady = false; // Protege contra escrita se o OLED falhar ou não iniciar
    unsigned long _unlockStartTime = 0;
    unsigned long _lastRfidReadTime = 0;
    CardReadCallback _onCardRead = nullptr;

    void lock();
    void checkRfid();
};

#endif