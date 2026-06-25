//
// auth.c - 登录校验
//

#include "DataService.h"

#include <string.h>

User* data_service_validate_login(const char* username, const char* password) {
    static User users[2] = {
        {"admin", "123456", 1},
        {"guest", "guest", 0}
    };
    for (int i = 0; i < 2; i++) {
        if (strcmp(username, users[i].name) == 0 && strcmp(password, users[i].password) == 0)
            return &users[i];
    }
    return NULL;
}
