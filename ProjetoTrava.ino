#include <ArduinoJson.h>
#include "UserStorage.h"
#include "Config.h"
#include "HardwareControl.h"
#include "MqttManager.h"
#include "BleManager.h"

UserStorage userStorage;
HardwareControl hardware;
MqttManager mqtt;
BleManager ble;

// Controle de inicialização diferida do BLE
bool bleStarted = false;
unsigned long bootStartTime = 0;
const unsigned long BLE_FALLBACK_TIMEOUT_MS = 15000; // 15s de tolerância caso a internet esteja offline

const String AUTHORIZED_UIDS[] = {"37AFF663", "0A16384F"};
const int NUM_UIDS = 2;

bool isUidAuthorized(const String& uid) {
    for (int i = 0; i < NUM_UIDS; i++) {
        if (AUTHORIZED_UIDS[i] == uid) return true;
    }
    return false;
}

// Callback 1: Cartão RFID lido
void handleCardRead(String uid) {
    Serial.printf("[RFID] Cartao detectado -> UID: %s\n", uid.c_str());

    if (isUidAuthorized(uid)) {
        Serial.println("[ACESSO] Autorizado! Acionando rele...");
        hardware.unlock();
        mqtt.publishEvent("{\"metodo\":\"RFID\",\"uid\":\"" + uid + "\",\"status\":\"AUTORIZADO\"}");
    } else {
        Serial.println("[ACESSO] Negado! UID nao cadastrado.");
        hardware.showMessage("ACESSO NEGADO", "UID: " + uid);
        mqtt.publishEvent("{\"metodo\":\"RFID\",\"uid\":\"" + uid + "\",\"status\":\"NEGADO\"}");
    }
}

// Callback 2: Comando remoto via Broker MQTT
void handleMqttCommand(String payload) {
    String cmd = payload;
    cmd.trim();

    // 1. Comando simples legado de abertura remota
    if (cmd == "ABRIR") {
        Serial.println("[MQTT] Comando remoto ABRIR recebido!");
        hardware.unlock();
        mqtt.publishEvent("{\"metodo\":\"MQTT_REMOTO\",\"status\":\"AUTORIZADO\"}");
        return;
    }

    // 2. Comandos JSON administrativos (NVS)
    if (cmd.startsWith("{")) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, cmd);
        if (err) {
            mqtt.publishEvent("{\"metodo\":\"NVS_ADMIN\",\"status\":\"ERRO_JSON\"}");
            return;
        }

        String acao = doc["acao"] | "";
        String usuario = doc["usuario"] | "";
        String chave = doc["chave"] | "";

        if (acao == "ADD_USER" || acao == "UPDATE_USER") {
            bool ok = userStorage.addOrUpdateUser(usuario, chave);
            mqtt.publishEvent("{\"metodo\":\"NVS_ADMIN\",\"acao\":\"" + acao + "\",\"usuario\":\"" + usuario + "\",\"sucesso\":" + String(ok ? "true" : "false") + "}");
        } 
        else if (acao == "DEL_USER") {
            bool ok = userStorage.removeUser(usuario);
            mqtt.publishEvent("{\"metodo\":\"NVS_ADMIN\",\"acao\":\"DEL_USER\",\"usuario\":\"" + usuario + "\",\"sucesso\":" + String(ok ? "true" : "false") + "}");
        } 
        else if (acao == "LIST_USERS") {
            String listaJson = userStorage.listUsersJson();
            mqtt.publishEvent("{\"metodo\":\"NVS_ADMIN\",\"acao\":\"LIST_USERS\",\"usuarios\":" + listaJson + "}");
        }
    }
}

// Callback 3: Resultado da autenticação Challenge-Response multi-usuário via BLE
void handleBleAuth(bool authorized, const String& userId) {
    if (authorized) {
        Serial.printf("[ACESSO] BLE autorizado para [%s]! Acionando rele...\n", userId.c_str());
        hardware.unlock();
        mqtt.publishEvent("{\"metodo\":\"BLE_HMAC\",\"usuario\":\"" + userId + "\",\"status\":\"AUTORIZADO\"}");
    } else {
        Serial.printf("[ACESSO] Falha BLE para [%s]!\n", userId.c_str());
        hardware.showMessage("ACESSO NEGADO", "User: " + userId);
        mqtt.publishEvent("{\"metodo\":\"BLE_HMAC\",\"usuario\":\"" + userId + "\",\"status\":\"NEGADO\"}");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== INICIANDO SISTEMA DA TRAVA ELETRONICA ===");

    hardware.setOnCardRead(handleCardRead);
    mqtt.setOnCommand(handleMqttCommand);
    ble.setOnAuthResult(handleBleAuth);

    hardware.begin();
    Serial.println("[HW] Hardware (Rele, OLED e RFID) inicializado.");

    mqtt.begin();
    userStorage.begin();
    ble.begin(&userStorage);
}

void loop() {
    hardware.update();
    mqtt.update();

    // Inicia o BLE somente após o MQTT conectar via TLS (ou após 15s se estiver sem internet)
    if (!bleStarted) {
        if (mqtt.isConnected() || (millis() - bootStartTime >= BLE_FALLBACK_TIMEOUT_MS)) {
            Serial.printf("[MEM] Heap livre antes do BLE: %d bytes\n", ESP.getFreeHeap());
            ble.begin(&userStorage);
            bleStarted = true;
        }
    } else {
        ble.update();
    }
}