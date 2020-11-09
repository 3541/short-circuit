#pragma once

#include "connection.h"
#include "http_types.h"
#include "uri.h"

#define HTTP_NEWLINE CS("\r\n")

typedef enum HttpConnectionState {
    CONNECTION_INIT,
    CONNECTION_PARSED_FIRST_LINE,
    CONNECTION_PARSED_HEADERS,
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
    fd          target_file;

    bool                 keep_alive;
    CString              host;
    HttpTransferEncoding transfer_encodings;
    ssize_t              content_length;

    HttpContentType      response_content_type;
    HttpTransferEncoding response_transfer_encodings;
} HttpConnection;

HttpConnection* http_connection_new();
void            http_connection_free(HttpConnection*, struct io_uring*);
void            http_connection_freelist_clear();

void http_connection_init(HttpConnection*);
void http_connection_reset(HttpConnection*);
