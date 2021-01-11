// Unfortunately, it seems that this is unviable, since OpenSSL after version
// 1.1.0 does not expose internal structures which we would need to access in
// order to initialize kTLS.
#include <assert.h>
#include <linux/tls.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/tls1.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

static void handshake(SSL* ssl, int sock) {
    char buf[16384] = { '\0' };
    ssize_t count;

    BIO* rbio = SSL_get_rbio(ssl);
    BIO* wbio = SSL_get_wbio(ssl);

    while (!SSL_is_init_finished(ssl)) {
        int rc = SSL_do_handshake(ssl);
        if (rc == 1)
            break;

        // OpenSSL lies and will return SSL_ERROR_WANT_READ when it actually
        // needs a write.
        if ((count = (ssize_t)BIO_ctrl_pending(wbio))) {
            if (BIO_read(wbio, buf, (int)count) != count) {
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }

            if (send(sock, buf, (size_t)count, 0) < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
        }

        switch (SSL_get_error(ssl, rc)) {
        case SSL_ERROR_WANT_READ:
            count = recv(sock, buf, sizeof(buf), 0);
            if (count < 1) {
                perror("recv");
                exit(EXIT_FAILURE);
            }

            if (BIO_write(rbio, buf, (int)count) < count) {
                fprintf(stderr, "BIO_write(%ld).\n", count);
                fprintf(stderr, "Should retry? %d\n", BIO_should_retry(rbio));
                ERR_print_errors_fp(stderr);
                exit(EXIT_FAILURE);
            }
            break;
        case SSL_ERROR_WANT_WRITE:
            // Next iteration.
            break;
        default:
            fprintf(stderr, "Unexpected error.\n");
            exit(EXIT_FAILURE);
        }
    }

    printf("Handshake done.\n");
}

static void ktls_init(SSL* ssl, int sock) {
    struct tls12_crypto_info_aes_gcm_128 crypto_info_aes_gcm128 = { .info = { .cipher_type = TLS_CIPHER_AES_GCM_128 } };
    struct tls12_crypto_info_aes_gcm_256 crypto_info_aes_gcm256 = { .info = { .cipher_type = TLS_CIPHER_AES_GCM_256 } };

    void* crypto_info = NULL;
    struct tls_crypto_info* tls_info = NULL;
    size_t iv_size, rec_seq_size, key_size, salt_size;
    uint8_t* iv = NULL;
    uint8_t* key = NULL;
    uint8_t* salt = NULL;
    uint8_t* rec_seq = NULL;

    switch (SSL_CIPHER_get_cipher_nid(SSL_get_current_cipher(ssl))) {
    case NID_aes_128_gcm:
        crypto_info = &crypto_info_aes_gcm128;
        iv = crypto_info_aes_gcm128.iv;
        iv_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
        key = crypto_info_aes_gcm128.key;
        key_size = TLS_CIPHER_AES_GCM_128_KEY_SIZE;
        salt = crypto_info_aes_gcm128.salt;
        salt_size = TLS_CIPHER_AES_GCM_128_SALT_SIZE;
        rec_seq = crypto_info_aes_gcm128.rec_seq;
        rec_seq_size = TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE;
        break;
    case NID_aes_256_gcm:
        crypto_info = &crypto_info_aes_gcm256;
        iv = crypto_info_aes_gcm256.iv;
        iv_size = TLS_CIPHER_AES_GCM_256_IV_SIZE;
        key = crypto_info_aes_gcm256.key;
        key_size = TLS_CIPHER_AES_GCM_256_KEY_SIZE;
        salt = crypto_info_aes_gcm256.salt;
        salt_size = TLS_CIPHER_AES_GCM_256_SALT_SIZE;
        rec_seq = crypto_info_aes_gcm256.rec_seq;
        rec_seq_size = TLS_CIPHER_AES_GCM_256_REC_SEQ_SIZE;
        break;
    }
    tls_info = crypto_info;

    switch (SSL_version(ssl)) {
    case TLS1_2_VERSION:
        tls_info->version = TLS_1_2_VERSION;
        break;
    case TLS1_3_VERSION:
        tls_info->version = TLS_1_3_VERSION;
        break;
    default:
        fprintf(stderr, "Unsupported TLS version.\n");
        exit(EXIT_FAILURE);
    }

    // It was at this moment that everything went horribly wrong.
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
        perror("setsockopt(SO_REUSEADDR)");
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

    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        ERR_print_errors_fp(stderr);
        goto done;
    }
    if (SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1) {
        ERR_print_errors_fp(stderr);
        goto done;
    }
    // TLS 1.2
    if (SSL_CTX_set_cipher_list(ctx, "AESGCM") != 1) {
        ERR_print_errors_fp(stderr);
        goto done;
    }
    // TLS 1.3
    if (SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256") != 1) {
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
    if (setsockopt(sock, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) {
        perror("setsockopt(tls)");
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
    SSL_set0_rbio(ssl, rbio);
    SSL_set0_wbio(ssl, wbio);

    handshake(ssl, sock);
    ktls_init(ssl, sock);

    printf("Done.\n");

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
