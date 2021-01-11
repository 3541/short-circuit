#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

static void handshake(SSL* ssl, BIO* rbio, BIO* wbio, int sock) {
    char buf[16384] = { '\0' };
    ssize_t count;

    while (!SSL_is_init_finished(ssl)) {
        int rc = SSL_accept(ssl);
        if (rc == 1)
            break;
        switch (SSL_get_error(ssl, rc)) {
        case SSL_ERROR_WANT_READ:
            count = recv(sock, buf, sizeof(buf), 0);
            if (count < 1) {
                perror("recv");
                exit(EXIT_FAILURE);
            }

            if (BIO_write(rbio, buf, (int)count) <= 0) {
                fprintf(stderr, "BIO_write(%ld).\n", count);
                fprintf(stderr, "Should retry? %d\n", BIO_should_retry(rbio));
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }
            break;
        case SSL_ERROR_WANT_WRITE:
            count = BIO_read(wbio, buf, (int)BIO_ctrl_pending(wbio));

            if (count < 0) {
                fprintf(stderr, "BIO_read.\n");
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }

            if (send(sock, buf, (size_t)count, 0) < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Unexpected error.\n");
            exit(EXIT_FAILURE);
        }
    }

    printf("Handshake done.\n");
}

int main(void) {
    int sock = -1;
    SSL* ssl = NULL;
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        return -1;
    }

    const int enable = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("setsockopt");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4433);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        return -1;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        goto done;
    }

    if (SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        goto done;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        goto done;
    }

    struct sockaddr_in peer;
    socklen_t peer_len;
    sock = accept(listen_sock, (struct sockaddr*)&peer, &peer_len);
    if (sock < 0) {
        perror("accept");
        goto done;
    }

    ssl = SSL_new(ctx);
    if (!ssl) {
        ERR_print_errors_fp(stderr);
        goto done;
    }

    SSL_set_accept_state(ssl);

    BIO* rbio = BIO_new(BIO_s_mem());
    BIO* wbio = BIO_new(BIO_s_mem());
    if (!rbio || !wbio) {
        fprintf(stderr, "BIO_new\n");
        ERR_print_errors_fp(stderr);
        goto done;
    }
    BIO_set_mem_eof_return(rbio, -1);
    BIO_set_mem_eof_return(wbio, -1);
    SSL_set_bio(ssl, rbio, wbio);

    handshake(ssl, rbio, wbio, sock);

/*
    char buf[20] = { '\0' };
    SSL_read(ssl, buf, sizeof(buf));

    SSL_write(ssl, buf, sizeof(buf));*/

done:
    if (ssl)
        SSL_free(ssl);
    if (ctx)
        SSL_CTX_free(ctx);
    if (sock >= 0 && close(sock) < 0)
        perror("close (sock)");
    if (listen_sock >= 0 && close(listen_sock) < 0)
        perror("close (listen)");

    return 0;
}
