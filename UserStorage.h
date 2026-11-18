#ifndef USER_STORAGE_H
#define USER_STORAGE_H

#include <Arduino.h>

class UserStorage {
public:
    void begin();
    bool addOrUpdateUser(const String& userId, const String& secret);
    bool removeUser(const String& userId);
    String getUserSecret(const String& userId);
    String listUsersJson();
    void factoryReset();

private:
    String getIndexList();
    void saveIndexList(const String& list);
};

#endif