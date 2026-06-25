//
// view_login.c - 登录界面与数据文件加载提示
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

static void read_password(char* buffer, int size) {
#ifdef _WIN32
    int i = 0;
    while (i < size - 1) {
        int c = _getch();
        if (c == '\r' || c == '\n') break;
        if (c == '\b' || c == 127) {
            if (i > 0) { i--; printf("\b \b"); }
        } else {
            buffer[i++] = (char)c;
            putchar('*');
        }
    }
    buffer[i] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int i = 0;
    while (i < size - 1) {
        char c = getchar();
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == '\b') {
            if (i > 0) { i--; printf("\b \b"); }
        } else {
            buffer[i++] = c;
            putchar('*');
        }
    }
    buffer[i] = '\0';
    printf("\n");
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
}

int view_show_login_screen(AppState* state) {
    view_clear_screen();
    printf("========================================\n");
    printf("   海水养殖水质数据分析系统 v1.0\n");
    printf("========================================\n\n");

    state->login_attempts = 0;
    while (state->login_attempts < 3) {
        char username[64], password[64];
        printf("请输入用户名: ");
        view_read_line(username, sizeof(username));
        printf("请输入密码: ");
        read_password(password, sizeof(password));

        User* user = data_service_validate_login(username, password);
        if (user != NULL) {
            state->current_user = *user;
            state->is_logged_in = 1;
            printf("\n========================================\n");
            printf("  登录成功！欢迎您，%s！\n", role_label(user->role));
            printf("========================================\n");
            view_pause();
            return 0;
        }

        state->login_attempts++;
        int remaining = 3 - state->login_attempts;
        if (remaining > 0)
            printf("\n用户名或密码错误！（剩余尝试次数：%d）\n\n", remaining);
    }
    printf("\n登录失败次数过多，程序退出。\n");
    return -1;
}

int view_prompt_load_file(AppState* state) {
    char filepath[512];
    printf("请输入数据文件路径（默认: dao/data_modify.csv）: ");
    view_read_line(filepath, sizeof(filepath));
    if (filepath[0] == '\0') strcpy(filepath, "dao/data_modify.csv");

    printf("正在加载数据文件 %s ...\n", filepath);
    if (!controller_load_data(state, filepath)) {
        printf("错误：无法读取文件 %s，请检查文件是否存在。\n", filepath);
        return 0;
    }
    printf("成功加载 %d 条记录。\n", state->current_dataset.size);
    view_pause();
    return 1;
}

int view_show_main_menu(AppState* state) {
    view_clear_screen();
    printf("========================================\n");
    printf("  海水养殖水质分析系统 v1.0\n");
    printf("  当前用户: %s (%s)\n", state->current_user.name, role_label(state->current_user.role));
    printf("  当前数据文件: %s (%d 条记录)\n", state->current_file_path, state->current_dataset.size);
    if (state->is_preprocessed)
        printf("  预处理状态: 已完成 (清洗后 %d 条)\n", state->clean_dataset.size);
    printf("========================================\n");

    if (state->current_user.role == 1) {
        printf("  [1] 数据基础操作\n");
        printf("  [2] 数据预处理\n");
        printf("  [3] 统计分析\n");
        printf("  [4] 预测分析\n");
        printf("  [5] 查看数据概览\n");
        printf("  [6] 查看预警报告\n");
        printf("  [7] 查看分析报告\n");
        printf("  [8] 数据备份与恢复\n");
        printf("  [9] 清屏\n");
        printf("  [0] 退出系统\n");
    } else {
        printf("  [1] 查看数据概览\n");
        printf("  [2] 查看预警报告\n");
        printf("  [3] 查看分析报告\n");
        printf("  [4] 清屏\n");
        printf("  [0] 退出系统\n");
    }
    printf("========================================\n");

    if (state->current_user.role == 1)
        return view_read_int("请选择操作 (0-9): ", 0, 9);
    else
        return view_read_int("请选择操作 (0-4): ", 0, 4);
}
