#include "protocol/hlp_core.h"

static void reset_receiver(hlp_receiver_t *receiver) {
    receiver->length = 0;
    receiver->overflow = false;
}

bool hlp_utf8_valid(const uint8_t *data, size_t length) {
    size_t index = 0;
    while (index < length) {
        const uint8_t first = data[index++];
        if (first < 0x80) continue;
        const size_t needed = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC2 ? 1 : 0;
        if (needed == 0 || index + needed > length) return false;
        uint32_t codepoint = first & ((1U << (6U - needed)) - 1U);
        for (size_t part_index = 0; part_index < needed; ++part_index) {
            const uint8_t part = data[index++];
            if ((part & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6U) | (part & 0x3F);
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            (needed == 1 && codepoint < 0x80) || (needed == 2 && codepoint < 0x800) ||
            (needed == 3 && codepoint < 0x10000)) return false;
    }
    return true;
}

void hlp_receiver_init(hlp_receiver_t *receiver, hlp_line_callback_t callback, void *context) {
    if (!receiver) return;
    *receiver = (hlp_receiver_t){0};
    receiver->callback = callback;
    receiver->context = context;
}

void hlp_receiver_feed(hlp_receiver_t *receiver, const uint8_t *data, size_t length) {
    if (!receiver || !data) return;
    for (size_t index = 0; index < length; ++index) {
        const uint8_t byte = data[index];
        if (byte == '\n') {
            if (receiver->overflow) {
                ++receiver->oversized;
            } else {
                size_t frame_length = receiver->length;
                if (frame_length != 0 && receiver->buffer[frame_length - 1] == '\r') --frame_length;
                if (!hlp_utf8_valid(receiver->buffer, frame_length)) {
                    ++receiver->malformed_utf8;
                } else if (receiver->callback) {
                    receiver->buffer[frame_length] = 0;
                    receiver->callback((const char *)receiver->buffer, frame_length, receiver->context);
                }
            }
            reset_receiver(receiver);
        } else if (!receiver->overflow) {
            if (receiver->length >= HLP_MAX_PAYLOAD) receiver->overflow = true;
            else receiver->buffer[receiver->length++] = byte;
        }
    }
}
