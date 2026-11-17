#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>

typedef void (*MqttCommandCallback)(String payload);

class MqttManager {
public:
    void begin();
    void update(); // Gerencia reconexões e pacotes sem travar o loop
    void publishEvent(const String& jsonPayload);
    bool isConnected();
    void setOnCommand(MqttCommandCallback cb);

private:
    unsigned long _lastReconnectAttempt = 0;
    MqttCommandCallback _onCommand = nullptr;

    void setupWifi();
    bool reconnectMqtt();
};

#endif