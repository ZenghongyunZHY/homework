//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_USER_H
#define HOMEWORK_USER_H
typedef struct {
    char* name;//用户名
    char* password;//密码
    int role;//用户角色，1为管理员，0为普通用户
} User;
#endif //HOMEWORK_USER_H
