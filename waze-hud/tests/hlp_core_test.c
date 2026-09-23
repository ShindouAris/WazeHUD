#include "protocol/hlp_core.h"

#include <assert.h>
#include <string.h>

static unsigned calls;
static char lines[3][HLP_MAX_FRAME];

static void capture(const char *line, size_t length, void *context) {
    (void)context;
    assert(calls < 3);
    assert(length < HLP_MAX_FRAME);
    memcpy(lines[calls], line, length + 1);
    ++calls;
}

int main(void) {
    hlp_receiver_t receiver;
    hlp_receiver_init(&receiver, capture, NULL);

    static const char first[] = "{\"v\":1,";
    static const char second[] = "\"t\":\"hi\"}\n{\"v\":1,\"t\":\"ping\"}\r\n";
    hlp_receiver_feed(&receiver, (const uint8_t *)first, sizeof(first) - 1);
    hlp_receiver_feed(&receiver, (const uint8_t *)second, sizeof(second) - 1);
    assert(calls == 2);
    assert(strcmp(lines[0], "{\"v\":1,\"t\":\"hi\"}") == 0);
    assert(strcmp(lines[1], "{\"v\":1,\"t\":\"ping\"}") == 0);

    uint8_t oversized[HLP_MAX_FRAME + 2];
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\n';
    hlp_receiver_feed(&receiver, oversized, sizeof(oversized));
    assert(receiver.oversized == 1);
    assert(calls == 2);

    hlp_receiver_feed(&receiver, (const uint8_t *)"partial", 7);
    hlp_receiver_init(&receiver, capture, NULL);
    hlp_receiver_feed(&receiver, (const uint8_t *)"{}\n", 3);
    assert(calls == 3);
    assert(strcmp(lines[2], "{}") == 0);
    return 0;
}
