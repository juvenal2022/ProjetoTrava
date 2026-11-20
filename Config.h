#ifndef CONFIG_H
#define CONFIG_H

// --- Configurações de Rede e Broker ---
//#define WIFI_SSID       "VIVOFIBRA-8D65"
//#define WIFI_PASS       "72238A8D65"

#define WIFI_SSID       "Fatec_Sala_Maker_2g"
#define WIFI_PASS       "Fatec@2026"

#define MQTT_BROKER     "u2101607.ala.us-east-1.emqxsl.com" // Substitua pelo seu broker
#define MQTT_PORT       8883
#define MQTT_USER       "jubabdag"
#define MQTT_PASS       "public"
#define DEVICE_ID       "ESP32_EMQX_Client"

// --- Certificado Raiz CA (Formato PEM) ---
// Substitua pelo CA Root Certificate fornecido pelo seu Broker (ex: EMQX Cloud, HiveMQ, Mosquitto)
static const char* CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

// --- Tópicos MQTT ---
#define TOPIC_CMD       "trava/" DEVICE_ID "/comando"
#define TOPIC_EVENT     "trava/" DEVICE_ID "/evento"
#define TOPIC_STATUS    "trava/" DEVICE_ID "/status"

// --- Pinagem de Hardware (ESP32) ---
#define PIN_RELAY       26
#define RELAY_ACTIVE    HIGH  // Mude para LOW se seu módulo relé ativar em nível baixo
#define UNLOCK_TIME_MS  3000  // Tempo em que a trava permanece aberta (3s)

// RFID RC522 (SPI Padrão: MOSI=23, MISO=19, SCK=18)
#define PIN_RFID_SS     5
#define PIN_RFID_RST    27

// Display OLED SSD1306 (I2C Padrão: SDA=21, SCL=22)
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C

// --- Configurações Bluetooth BLE (Challenge-Response Multi-Usuário) ---
#define BLE_DEVICE_NAME   "Trava_ESP32_01"
#define BLE_SERVICE_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_TX_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a7" // Leitura do Nonce
#define BLE_CHAR_RX_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8" // Escrita de usuario_id:hmac
#define BLE_CHAR_STATUS_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define BLE_NONCE_TTL_MS  15000

#define BLE_ADMIN_USER_ID     "admin_01"
#define BLE_NVS_NAMESPACE     "ble_users"
#define BLE_DEFAULT_ADMIN_ID  "admin_01"
#define BLE_DEFAULT_ADMIN_KEY "12345678"

#endif