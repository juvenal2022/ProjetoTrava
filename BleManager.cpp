#include "BleManager.h"
#include "Config.h"
#include <NimBLEDevice.h>
#include "mbedtls/md.h"
#include "esp_system.h"

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

static const char* findUserSecret(const String& userId) {
    for (int i = 0; i < NUM_BLE_USERS; i++) {
        if (userId == BLE_USERS[i].id) {
            return BLE_USERS[i].secret;
        }
    }
    return nullptr;
}

static void issueNewChallenge() {
    currentNonce = generateHexNonce();
    nonceCreatedAt = millis();
    nonceActive = true;

    if (pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((uint8_t*)currentNonce.c_str(), currentNonce.length());
    }
    Serial.printf("[BLE-AUTH] Novo desafio gerado (%d bytes): %s\n", currentNonce.length(), currentNonce.c_str());
}

class LockServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("[BLE] Celular conectado.");
        // Se por acaso não houver nonce ativo (ex: expirou por timeout), gera um agora
        if (!nonceActive || currentNonce.isEmpty()) {
            issueNewChallenge();
        }
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        Serial.println("[BLE] Celular desconectado.");
        // Gera um novo desafio já pronto para a próxima conexão (evita race condition no próximo onConnect)
        issueNewChallenge();
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
            nonceActive = false;
            if (globalAuthCallback != nullptr) globalAuthCallback(false, "FORMATO_INVALIDO");
            issueNewChallenge();
            return;
        }

        String userId = payload.substring(0, sepIndex);
        String receivedHmac = payload.substring(sepIndex + 1);
        userId.trim();
        receivedHmac.trim();
        receivedHmac.toLowerCase();

        // 1. Verifica validade temporal do desafio
        if (!nonceActive || (millis() - nonceCreatedAt > BLE_NONCE_TTL_MS)) {
            Serial.printf("[BLE-AUTH] Falha (%s): Nonce expirado ou inexistente.\n", userId.c_str());
            nonceActive = false;
            if (globalAuthCallback != nullptr) globalAuthCallback(false, userId);
            issueNewChallenge();
            return;
        }

        // 2. Consome o nonce imediatamente
        String usedNonce = currentNonce;
        nonceActive = false;
        currentNonce = "";

        // 3. Busca a chave individual do usuario_id
        const char* userSecret = findUserSecret(userId);
        if (userSecret == nullptr) {
            Serial.printf("[BLE-AUTH] Usuario desconhecido: %s\n", userId.c_str());
            if (globalAuthCallback != nullptr) globalAuthCallback(false, userId);
            issueNewChallenge();
            return;
        }

        // 4. Calcula HMAC-SHA256(userSecret, "usuario_id:nonce")
        String messageToSign = userId + ":" + usedNonce;
        String expectedHmac = computeHmacSha256(String(userSecret), messageToSign);
        bool valid = constantTimeEquals(receivedHmac, expectedHmac);

        if (valid) {
            Serial.printf("[BLE-AUTH] Assinatura valida para [%s]! Acesso autorizado.\n", userId.c_str());
        } else {
            Serial.printf("[BLE-AUTH] Assinatura invalida para [%s]!\n", userId.c_str());
        }

        if (globalAuthCallback != nullptr) {
            globalAuthCallback(valid, userId);
        }

        issueNewChallenge();
    }
};

void BleManager::begin() {
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

    pRxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new LockCharCallbacks());

    pService->start();
    issueNewChallenge();

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
        nonceActive = false;
        currentNonce = "";
        Serial.println("[BLE-AUTH] Nonce expirado por timeout.");
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