#include "UserStorage.h"
#include "Config.h"
#include <Preferences.h>

static Preferences prefs;

void UserStorage::begin() {
    prefs.begin(BLE_NVS_NAMESPACE, false);

    // Se for o primeiro boot (NVS vazio), cadastra o administrador padrão
    String index = getIndexList();
    if (index.isEmpty()) {
        Serial.println("[NVS] Inicializando usuario admin padrao...");
        addOrUpdateUser(BLE_DEFAULT_ADMIN_ID, BLE_DEFAULT_ADMIN_KEY);
    } else {
        Serial.printf("[NVS] Usuarios BLE carregados: %s\n", index.c_str());
    }
}

String UserStorage::getIndexList() {
    return prefs.getString("__index__", "");
}

void UserStorage::saveIndexList(const String& list) {
    prefs.putString("__index__", list);
}

bool UserStorage::addOrUpdateUser(const String& userId, const String& secret) {
    // Limite de 15 caracteres para chaves NVS no ESP32
    if (userId.isEmpty() || userId.length() > 15 || userId == "__index__" || secret.length() < 8) {
        Serial.println("[NVS] Erro: ID invalido (1-15 chars) ou chave muito curta (min 8 chars).");
        return false;
    }

    // Salva ou sobrescreve a chave secreta do usuário
    prefs.putString(userId.c_str(), secret);

    // Atualiza o índice caso o usuário ainda não exista nele
    String index = getIndexList();
    String paddedIndex = "," + index + ",";
    String target = "," + userId + ",";

    if (paddedIndex.indexOf(target) == -1) {
        if (!index.isEmpty()) index += ",";
        index += userId;
        saveIndexList(index);
    }

    Serial.printf("[NVS] Usuario [%s] salvo/atualizado com sucesso.\n", userId.c_str());
    return true;
}

bool UserStorage::removeUser(const String& userId) {
    if (userId.isEmpty() || userId == "__index__") return false;

    if (!prefs.isKey(userId.c_str())) {
        Serial.printf("[NVS] Usuario [%s] nao encontrado para remocao.\n", userId.c_str());
        return false;
    }

    prefs.remove(userId.c_str());

    // Reconstrói o índice sem o userId removido
    String index = getIndexList();
    String newIndex = "";
    int start = 0;

    while (start < (int)index.length()) {
        int comma = index.indexOf(',', start);
        String token = (comma == -1) ? index.substring(start) : index.substring(start, comma);
        if (token.length() > 0 && token != userId) {
            if (!newIndex.isEmpty()) newIndex += ",";
            newIndex += token;
        }
        if (comma == -1) break;
        start = comma + 1;
    }

    saveIndexList(newIndex);
    Serial.printf("[NVS] Usuario [%s] removido da NVS.\n", userId.c_str());
    return true;
}

String UserStorage::getUserSecret(const String& userId) {
    if (userId.isEmpty() || userId == "__index__") return "";
    return prefs.getString(userId.c_str(), "");
}

String UserStorage::listUsersJson() {
    String index = getIndexList();
    String json = "[";
    int start = 0;
    bool first = true;

    while (start < (int)index.length()) {
        int comma = index.indexOf(',', start);
        String token = (comma == -1) ? index.substring(start) : index.substring(start, comma);
        if (token.length() > 0) {
            if (!first) json += ",";
            json += "\"" + token + "\"";
            first = false;
        }
        if (comma == -1) break;
        start = comma + 1;
    }

    json += "]";
    return json;
}

void UserStorage::factoryReset() {
    prefs.clear();
    addOrUpdateUser(BLE_DEFAULT_ADMIN_ID, BLE_DEFAULT_ADMIN_KEY);
    Serial.println("[NVS] Reset de fabrica concluido.");
}