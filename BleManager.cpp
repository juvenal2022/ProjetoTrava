#include "BleManager.h"
#include "Config.h"
#include <NimBLEDevice.h>
#include "mbedtls/md.h"
#include "esp_system.h"

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr; // Envia o Nonce
static NimBLECharacteristic* pRxCharacteristic = nullptr; // Recebe o HMAC
static BleAuthCallback globalAuthCallback = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;

// Estado do desafio atual (uso único)
static String currentNonce = "";
static unsigned long nonceCreatedAt = 0;
static bool nonceActive = false;

// Gera 16 bytes aleatórios via hardware RNG e converte para 32 chars HEX
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

// Calcula HMAC-SHA256 usando mbedTLS nativo do ESP32
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

// Comparação em tempo constante para evitar timing attacks
static bool constantTimeEquals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (unsigned int i = 0; i < a.length(); i++) {
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    }
    return diff == 0;
}

// Gera um novo desafio e atualiza a característica TX
static void issueNewChallenge() {
    currentNonce = generateHexNonce();
    nonceCreatedAt = millis();
    nonceActive = true;

    if (pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((uint8_t*)currentNonce.c_str(), currentNonce.length());
    }
    Serial.printf("[BLE-AUTH] Novo desafio gerado: %s\n", currentNonce.c_str());
}

class LockServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("[BLE] Celular conectado.");
        issueNewChallenge(); // Prepara o desafio assim que o cliente conecta
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        nonceActive = false;
        currentNonce = "";
        Serial.println("[BLE] Celular desconectado. Nonce invalidado.");
    }
};

class LockCharCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue val = pCharacteristic->getValue();
        String receivedHmac = "";
        for (size_t i = 0; i < val.length(); i++) {
            receivedHmac += (char)val[i];
        }
        receivedHmac.trim();
        receivedHmac.toLowerCase();

        // 1. Verifica se existe um desafio ativo e dentro do prazo
        if (!nonceActive || (millis() - nonceCreatedAt > BLE_NONCE_TTL_MS)) {
            Serial.println("[BLE-AUTH] Falha: Nonce expirado ou inexistente.");
            nonceActive = false;
            if (globalAuthCallback != nullptr) globalAuthCallback(false);
            issueNewChallenge(); // Emite novo desafio para nova tentativa
            return;
        }

        // 2. Consome o nonce imediatamente (impede replay)
        String usedNonce = currentNonce;
        nonceActive = false;
        currentNonce = "";

        // 3. Calcula e valida o HMAC-SHA256 esperado
        String expectedHmac = computeHmacSha256(BLE_SHARED_SECRET, usedNonce);
        bool valid = constantTimeEquals(receivedHmac, expectedHmac);

        if (valid) {
            Serial.println("[BLE-AUTH] Assinatura HMAC valida! Acesso autorizado.");
        } else {
            Serial.println("[BLE-AUTH] Assinatura HMAC invalida!");
        }

        if (globalAuthCallback != nullptr) {
            globalAuthCallback(valid);
        }

        // Emite um novo desafio caso o usuário precise abrir novamente na mesma sessão
        issueNewChallenge();
    }
};

void BleManager::begin() {
    Serial.printf("[BLE] Iniciando servidor NimBLE seguro: %s\n", BLE_DEVICE_NAME);

    NimBLEDevice::init(BLE_DEVICE_NAME);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new LockServerCallbacks());

    NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    // Característica TX: celular lê (READ) ou escuta (NOTIFY) o nonce
    pTxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_TX_UUID,
        NIMBLE_PROPERTY::READ
    );

    // Característica RX: celular envia o HMAC-SHA256(secret, nonce)
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

    Serial.println("[BLE] Aguardando conexoes via Bluetooth (Challenge-Response)...");
}

void BleManager::setOnAuthResult(BleAuthCallback cb) {
    _onAuthResult = cb;
    globalAuthCallback = cb;
}

bool BleManager::isConnected() {
    return deviceConnected;
}

void BleManager::update() {
    // Expira o nonce caso o cliente conecte e demore mais de 15s sem responder
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