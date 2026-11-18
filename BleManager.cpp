#include "BleManager.h"
#include "Config.h"
#include <NimBLEDevice.h>
#include "mbedtls/md.h"
#include "esp_system.h"

static UserStorage* globalStorage = nullptr;
static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static NimBLECharacteristic* pRxCharacteristic = nullptr;
static BleAuthCallback globalAuthCallback = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;

static String currentNonce = "";
static unsigned long nonceCreatedAt = 0;
static bool nonceActive = false;

static String generateHexNonce() {
    uint8_t randomBytes[16];
    esp_fill_random(randomBytes, sizeof(randomBytes));

    char hexStr[33];
    for (int i = 0; i < 16; i++) {
        sprintf(&hexStr[i * 2], "%02x", randomBytes[i]);
    }
    hexStr[32] = '\0';
    return String(hexStr);
}

static String computeHmacSha256(const String& key, const String& data) {
    uint8_t hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    char hexOut[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hexOut[i * 2], "%02x", hmacResult[i]);
    }
    hexOut[64] = '\0';
    return String(hexOut);
}

static bool constantTimeEquals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) {
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    }
    return diff == 0;
}

static void issueNewChallenge() {
    currentNonce = generateHexNonce();
    nonceCreatedAt = millis();
    nonceActive = true;

    if (pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((uint8_t*)currentNonce.c_str(), currentNonce.length());
        pTxCharacteristic->notify();
    }
    Serial.printf("[BLE-AUTH] Novo desafio gerado (%d bytes): %s\n", currentNonce.length(), currentNonce.c_str());
}

// Limpa o desafio da memória e do buffer GATT
static void clearChallenge() {
    nonceActive = false;
    currentNonce = "";
    if (pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((uint8_t*)"", 0);
    }
}

class LockServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("[BLE] Celular conectado.");
        clearChallenge();
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        Serial.println("[BLE] Celular desconectado.");
        clearChallenge();
    }
};

class LockCharCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pCharacteristic->getValue();
        String payload = "";
        for (size_t i = 0; i < val.length(); i++) {
            payload += (char)val[i];
        }
        payload.trim();

        Serial.printf("[BLE-AUTH] Payload recebido (%d bytes): %s\n", payload.length(), payload.c_str());

        // Formato esperado: usuario_id:hmac_hex
        int sepIndex = payload.indexOf(':');
        if (sepIndex <= 0) {
            Serial.println("[BLE-AUTH] Formato invalido. Esperado usuario_id:hmac");
            clearChallenge();
            if (globalAuthCallback != nullptr) globalAuthCallback(false, "FORMATO_INVALIDO");
            return;
        }

        String userId = payload.substring(0, sepIndex);
        String receivedHmac = payload.substring(sepIndex + 1);
        userId.trim();
        receivedHmac.trim();
        receivedHmac.toLowerCase();

       // Passo 2: Verifica se existe desafio ativo e dentro do prazo de 15s
        if (!nonceActive || (millis() - nonceCreatedAt > BLE_NONCE_TTL_MS)) {
            Serial.printf("[BLE-AUTH] Falha (%s): Desafio nao solicitado ou expirado.\n", userId.c_str());
            clearChallenge();
            if (globalAuthCallback != nullptr) globalAuthCallback(false, userId);
            return;
        }

        // Passo 3: Consome e invalida o nonce imediatamente (uso único)
        String usedNonce = currentNonce;
        clearChallenge();

        // Passo 4: Consulta chave na NVS e valida HMAC-SHA256
        String userSecret = (globalStorage != nullptr) ? globalStorage->getUserSecret(userId) : "";
        if (userSecret.isEmpty()) {
            Serial.printf("[BLE-AUTH] Usuario desconhecido na NVS: %s\n", userId.c_str());
            if (globalAuthCallback != nullptr) globalAuthCallback(false, userId);
            return;
        }

        String messageToSign = userId + ":" + usedNonce;
        String expectedHmac = computeHmacSha256(userSecret, messageToSign);
        bool valid = constantTimeEquals(receivedHmac, expectedHmac);

        if (valid) {
            Serial.printf("[BLE-AUTH] Assinatura valida para [%s]! Acesso autorizado.\n", userId.c_str());
        } else {
            Serial.printf("[BLE-AUTH] Assinatura invalida para [%s]!\n", userId.c_str());
        }

        if (globalAuthCallback != nullptr) {
            globalAuthCallback(valid, userId);
        }
    }
};

class TxCharCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // Gera o desafio fresco no exato milissegundo em que o celular solicita a leitura
        currentNonce = generateHexNonce();
        nonceCreatedAt = millis();
        nonceActive = true;

        pCharacteristic->setValue((uint8_t*)currentNonce.c_str(), currentNonce.length());
        Serial.printf("[BLE-AUTH] Desafio gerado sob demanda no onRead (%d bytes): %s\n",
                      currentNonce.length(), currentNonce.c_str());
    }
};

void BleManager::begin(UserStorage* storage) {
    globalStorage = storage;
    Serial.printf("[BLE] Iniciando servidor NimBLE multi-usuario: %s\n", BLE_DEVICE_NAME);

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(256); // Garante recebimento de payloads maiores que 20/64 bytes
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new LockServerCallbacks());

    NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    pTxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_TX_UUID,
        NIMBLE_PROPERTY::READ
    );
    pTxCharacteristic->setCallbacks(new TxCharCallbacks());

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

    Serial.println("[BLE] Aguardando conexoes via Bluetooth (Multi-Usuario)...");
}

void BleManager::setOnAuthResult(BleAuthCallback cb) {
    _onAuthResult = cb;
    globalAuthCallback = cb;
}

bool BleManager::isConnected() {
    return deviceConnected;
}

void BleManager::update() {
    if (nonceActive && (millis() - nonceCreatedAt > BLE_NONCE_TTL_MS)) {
        clearChallenge();
        Serial.println("[BLE-AUTH] Desafio expirado por inatividade do cliente.");
    }

    if (!deviceConnected && oldDeviceConnected) {
        NimBLEDevice::startAdvertising();
        Serial.println("[BLE] Advertising reiniciado.");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}