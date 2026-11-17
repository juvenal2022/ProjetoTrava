#include "BleManager.h"
#include "Config.h"
#include <NimBLEDevice.h>

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pRxCharacteristic = nullptr;
static BleCommandCallback globalBleCallback = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;

class LockServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("[BLE] Celular conectado.");
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        Serial.println("[BLE] Celular desconectado.");
    }
};

class LockCharCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pCharacteristic->getValue();
        String msg = "";
        for (size_t i = 0; i < val.length(); i++) {
            msg += (char)val[i];
        }
        msg.trim();

        Serial.printf("[BLE] Bytes recebidos (%d): '%s'\n", val.length(), msg.c_str());

        if (msg.length() > 0 && globalBleCallback != nullptr) {
            globalBleCallback(msg);
        }
    }
};

void BleManager::begin() {
    Serial.printf("[BLE] Iniciando servidor NimBLE: %s\n", BLE_DEVICE_NAME);

    NimBLEDevice::init(BLE_DEVICE_NAME);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new LockServerCallbacks());

    NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    pRxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new LockCharCallbacks());

    pService->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

    NimBLEAdvertisementData advData;
    advData.setName(BLE_DEVICE_NAME);
    advData.setCompleteServices(NimBLEUUID(BLE_SERVICE_UUID));
    pAdvertising->setAdvertisementData(advData);
    
    NimBLEAdvertisementData scanRespData;
    scanRespData.setName(BLE_DEVICE_NAME);
    pAdvertising->setScanResponseData(scanRespData);
    
    pAdvertising->start();

    Serial.println("[BLE] Aguardando conexoes via Bluetooth (NimBLE)...");
}

void BleManager::setOnCommand(BleCommandCallback cb) {
    _onCommand = cb;
    globalBleCallback = cb;
}

bool BleManager::isConnected() {
    return deviceConnected;
}

void BleManager::update() {
    if (!deviceConnected && oldDeviceConnected) {
        NimBLEDevice::startAdvertising();
        Serial.println("[BLE] Advertising reiniciado.");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}