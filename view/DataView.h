//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_DATAVIEW_H
#define HOMEWORK_DATAVIEW_H

#include "../controller/DataController.h"

/* login */
int view_show_login_screen(AppState* state);

/* file loading */
int view_prompt_load_file(AppState* state);

/* main menu */
int view_show_main_menu(AppState* state);

/* sub-menu handlers */
void view_handle_data_operations(AppState* state);
void view_handle_preprocess(AppState* state);
void view_handle_statistics(AppState* state);
void view_handle_prediction(AppState* state);
void view_handle_overview(AppState* state);
void view_handle_warnings(AppState* state);
void view_handle_reports(AppState* state);
void view_handle_backup(AppState* state);

/* utilities */
void view_clear_screen(void);
int view_confirm(const char* prompt);
void view_pause(void);

#endif // HOMEWORK_DATAVIEW_H
