#include "status_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *history_strdup(const char *text) {
    size_t len;
    char *copy;

    if (text == 0) {
        return 0;
    }

    len = strlen(text);
    copy = (char *)malloc(len + 1U);
    if (copy == 0) {
        return 0;
    }

    memcpy(copy, text, len + 1U);
    return copy;
}

bool status_history_init(StatusHistory *history, size_t capacity) {
    if (history == 0 || capacity == 0U) {
        return false;
    }

    history->entries = (char **)calloc(capacity, sizeof(char *));
    if (history->entries == 0) {
        return false;
    }

    history->count = 0U;
    history->capacity = capacity;
    history->last_qr[0] = '\0';
    history->current_qr[0] = '\0';
    return true;
}

void status_history_destroy(StatusHistory *history) {
    size_t i;

    if (history == 0) {
        return;
    }

    for (i = 0; i < history->count; ++i) {
        free(history->entries[i]);
    }
    free(history->entries);

    history->entries = 0;
    history->count = 0U;
    history->capacity = 0U;
}

void status_history_clear(StatusHistory *history) {
    size_t i;

    if (history == 0) {
        return;
    }

    for (i = 0; i < history->count; ++i) {
        free(history->entries[i]);
        history->entries[i] = 0;
    }
    history->count = 0U;
    history->last_qr[0] = '\0';
    history->current_qr[0] = '\0';
}

bool status_history_append(StatusHistory *history, const char *timestamp, const char *qr_text) {
    char line[APP_QR_TEXT_CAPACITY + 64];
    char *copy;

    if (history == 0 || timestamp == 0 || qr_text == 0) {
        return false;
    }

    if (snprintf(line, sizeof(line), "[%s] %s", timestamp, qr_text) < 0) {
        return false;
    }

    copy = history_strdup(line);
    if (copy == 0) {
        return false;
    }

    if (history->count == history->capacity) {
        free(history->entries[0]);
        memmove(
            &history->entries[0],
            &history->entries[1],
            sizeof(char *) * (history->capacity - 1U)
        );
        history->count -= 1U;
    }

    history->entries[history->count] = copy;
    history->count += 1U;

    strncpy(history->last_qr, qr_text, sizeof(history->last_qr) - 1U);
    history->last_qr[sizeof(history->last_qr) - 1U] = '\0';
    strncpy(history->current_qr, qr_text, sizeof(history->current_qr) - 1U);
    history->current_qr[sizeof(history->current_qr) - 1U] = '\0';
    return true;
}
