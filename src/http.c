#include "http.h"

#include <assert.h>

#include "util.h"

int8_t http_request_parse(struct Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    PANIC("TODO");
}
