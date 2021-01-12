// Same sort of test as ossl_test, with GnuTLS, instead.

#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <gnutls/gnutls.h>

int main(void) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        return -1;
    }

    const int enable = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(4433);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }
    if (listen(listen_sock, 10) < 0) {
        perror("listen");
        return -1;
    }

    int                                   rc = -1;
    gnutls_certificate_server_credentials cred;
    if ((rc = gnutls_certificate_allocate_credentials(&cred)) < 0) {
        fprintf(stderr, "allocate_credentials: %s\n", gnutls_strerror(rc));
        return -1;
    }
    if ((rc = gnutls_certificate_set_x509_key_file(cred, "cert.pem", "key.pem",
                                                   GNUTLS_X509_FMT_PEM)) < 0) {
        fprintf(stderr, "set_x509_key_file: %s\n", gnutls_strerror(rc));
        return -1;
    }

    struct sockaddr_in peer;
    socklen_t          peer_len;
    int sock = accept(listen_sock, (struct sockaddr*)&peer, &peer_len);
    if (sock < 0) {
        perror("accept");
        return -1;
    }

    gnutls_session_t session;
    if ((rc = gnutls_init(&session, GNUTLS_SERVER)) < 0) {
        fprintf(stderr, "gnutls_init: %s\n", gnutls_strerror(rc));
        return -1;
    }
    gnutls_set_default_priority(session);
    gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, cred);
    gnutls_transport_set_int(session, sock);

    if ((rc = gnutls_handshake(session)) < 0) {
        fprintf(stderr, "gnutls_handshake: %s\n", gnutls_strerror(rc));
        return -1;
    }

    printf("Handshake done.\n");

    char    buf[128] = { '\0' };
    ssize_t count    = -1;
    if ((count = gnutls_record_recv(session, buf, sizeof(buf))) < 0) {
        fprintf(stderr, "record_recv: %s\n", gnutls_strerror((int)count));
        return -1;
    }

    if ((rc = (int)gnutls_record_send(session, buf, (size_t)count)) < 0) {
        fprintf(stderr, "record_send: %s\n", gnutls_strerror(rc));
        return -1;
    }

    printf("Done.\n");

    gnutls_bye(session, GNUTLS_SHUT_RDWR);
    gnutls_deinit(session);
    gnutls_certificate_free_credentials(cred);
}
