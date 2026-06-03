#ifndef STATUS_HISTORY_H
#define STATUS_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

#include "app_types.h"

typedef struct StatusHistory {
    char **entries;
    size_t count;
    size_t capacity;
    char last_qr[APP_QR_TEXT_CAPACITY];
    char current_qr[APP_QR_TEXT_CAPACITY];
} StatusHistory;

bool status_history_init(StatusHistory *history, size_t capacity);
void status_history_destroy(StatusHistory *history);
void status_history_clear(StatusHistory *history);
bool status_history_append(StatusHistory *history, const char *timestamp, const char *qr_text);

#endif
