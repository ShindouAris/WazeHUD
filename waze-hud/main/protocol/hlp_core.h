#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HLP_MAX_FRAME 512
#define HLP_MAX_PAYLOAD (HLP_MAX_FRAME - 1)

typedef void (*hlp_line_callback_t)(const char *line, size_t length, void *context);

typedef struct {
    uint8_t buffer[HLP_MAX_FRAME];
    size_t length;
    bool overflow;
    uint32_t oversized;
    uint32_t malformed_utf8;
    hlp_line_callback_t callback;
    void *context;
} hlp_receiver_t;

void hlp_receiver_init(hlp_receiver_t *receiver, hlp_line_callback_t callback, void *context);
void hlp_receiver_feed(hlp_receiver_t *receiver, const uint8_t *data, size_t length);
bool hlp_utf8_valid(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
