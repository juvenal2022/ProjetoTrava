#include "MqttManager.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

static MqttCommandCallback globalCmdCallback = nullptr;
static bool ntpConfigured = false;
static bool wifiLogged = false;
static bool ntpLogged = false;

static void internalMqttCallback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.printf("[MQTT] Mensagem recebida em [%s]: %s\n", topic, message.c_str());

    if (globalCmdCallback != nullptr && String(topic) == TOPIC_CMD) {
        globalCmdCallback(message);
    }
}

void MqttManager::begin() {
    setupWifi();
    
    espClient.setCACert(CA_CERT);
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(internalMqttCallback);
    mqttClient.setBufferSize(512);
}

void MqttManager::setOnCommand(MqttCommandCallback cb) {
    _onCommand = cb;
    globalCmdCallback = cb;
}

void MqttManager::setupWifi() {
    Serial.printf("[WIFI] Conectando a rede: %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool MqttManager::isConnected() {
    return mqttClient.connected();
}

static bool isTimeSynced() {
    if (!ntpConfigured) {
        Serial.println("[NTP] Iniciando sincronizacao de horario...");
        configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");
        ntpConfigured = true;
    }

    time_t now = time(nullptr);
    if (now > 1704067200) {
        if (!ntpLogged) {
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char timeStr[32];
            strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M:%S", &timeinfo);
            Serial.printf("[NTP] Horario sincronizado: %s\n", timeStr);
            ntpLogged = true;
        }
        return true;
    }
    return false;
}

bool MqttManager::reconnectMqtt() {
    Serial.printf("[MQTT] Conectando ao broker %s:%d via TLS...\n", MQTT_BROKER, MQTT_PORT);

    if (mqttClient.connect(DEVICE_ID, MQTT_USER, MQTT_PASS, TOPIC_STATUS, 1, true, "OFFLINE")) {
        Serial.println("[MQTT] Conectado com sucesso via TLS!");
        mqttClient.publish(TOPIC_STATUS, "ONLINE", true);
        mqttClient.subscribe(TOPIC_CMD);
        Serial.printf("[MQTT] Inscrito no topico: %s\n", TOPIC_CMD);
        return true;
    } else {
        int mqttState = mqttClient.state();
        char tlsErr[100];
        espClient.lastError(tlsErr, sizeof(tlsErr));

        Serial.printf("[MQTT] Falha na conexao! Codigo PubSubClient: %d | Erro TLS: %s\n", mqttState, tlsErr);
        return false;
    }
}

void MqttManager::publishEvent(const String& jsonPayload) {
    if (mqttClient.connected()) {
        mqttClient.publish(TOPIC_EVENT, jsonPayload.c_str());
        Serial.printf("[MQTT] Evento publicado em [%s]: %s\n", TOPIC_EVENT, jsonPayload.c_str());
    } else {
        Serial.println("[MQTT] Aviso: offline, evento nao enviado ao broker.");
    }
}

void MqttManager::update() {
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLogged) {
            Serial.println("[WIFI] Conexao perdida! Tentando reconectar...");
            wifiLogged = false;
        }
        return;
    }

    if (!wifiLogged) {
        Serial.printf("[WIFI] Conectado! IP obtido: %s\n", WiFi.localIP().toString().c_str());
        wifiLogged = true;
    }

    if (!isTimeSynced()) {
        return;
    }

    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            if (reconnectMqtt()) {
                _lastReconnectAttempt = 0;
            }
        }
    } else {
        mqttClient.loop();
    }
}