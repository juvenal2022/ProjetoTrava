#include "HardwareControl.h"
#include "Config.h"
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

void HardwareControl::begin() {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, !RELAY_ACTIVE); // Tranca fisicamente antes de mexer no OLED

    // 1. Inicializa o display OLED primeiro
    Wire.begin();
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        _displayReady = true;
    } else {
        Serial.println("[ERRO] Falha ao iniciar display OLED no endereco 0x3C");
    }

    // 2. Agora é seguro chamar lock() e showMessage()
    lock();

    // 3. Inicializa o RFID
    SPI.begin();
    mfrc522.PCD_Init();
}

void HardwareControl::setOnCardRead(CardReadCallback cb) {
    _onCardRead = cb;
}

void HardwareControl::unlock() {
    digitalWrite(PIN_RELAY, RELAY_ACTIVE);
    _isUnlocked = true;
    _unlockStartTime = millis();
    showMessage("ACESSO LIBERADO", "Trava aberta");
}

void HardwareControl::lock() {
    digitalWrite(PIN_RELAY, !RELAY_ACTIVE);
    _isUnlocked = false;
    showMessage("Sistema Pronto", "Aproxime o cartao");
}

void HardwareControl::showMessage(const String& line1, const String& line2) {
    if (!_displayReady) return; // Evita o StoreProhibited se o OLED não estiver pronto

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    display.setTextSize(1);
    display.setCursor(0, 12);
    display.println(line1);
    
    if (line2.length() > 0) {
        display.setCursor(0, 34);
        display.println(line2);
    }
    display.display();
}

void HardwareControl::checkRfid() {
    if (millis() - _lastRfidReadTime < 1500) return;

    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        return;
    }

    _lastRfidReadTime = millis();

    String uid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    if (_onCardRead != nullptr) {
        _onCardRead(uid);
    }
}

void HardwareControl::update() {
    if (_isUnlocked && (millis() - _unlockStartTime >= UNLOCK_TIME_MS)) {
        lock();
    }

    checkRfid();
}