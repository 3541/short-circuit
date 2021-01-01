#pragma once

#include <liburing.h>
#include <stdbool.h>
#include <sys/types.h>

#include <a3/str.h>

#include "../connection.h"
#include "file.h"
#include "http/types.h"
#include "socket.h"
#include "uri.h"

#define HTTP_NEWLINE CS("\r\n")

typedef enum HttpConnectionState {
    CONNECTION_INIT,
    CONNECTION_PARSED_FIRST_LINE,
    CONNECTION_PARSED_HEADERS,
    CONNECTION_OPENING_FILE,
    CONNECTION_RESPONDING,
    CONNECTION_CLOSING,
} HttpConnectionState;

typedef struct HttpConnection {
    Connection conn;

    HttpConnectionState state;

    HttpVersion version;
    HttpMethod  method;
    Uri         target;
    String      target_path;
    FileHandle* target_file;

    bool                 keep_alive;
    String               host;
    HttpTransferEncoding transfer_encodings;
    ssize_t              content_length;

    HttpContentType      response_content_type;
    HttpTransferEncoding response_transfer_encodings;
} HttpConnection;

void            http_connection_pool_init(void);
HttpConnection* http_connection_new(void);
void            http_connection_free(HttpConnection*, struct io_uring*);
void            http_connection_pool_free(void);

bool http_connection_init(HttpConnection*);
bool http_connection_reset(HttpConnection*, struct io_uring*);
