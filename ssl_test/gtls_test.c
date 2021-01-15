// Same sort of test as ossl_test, with GnuTLS, instead.

#define _GNU_SOURCE // For SOL_TCP.

#include <assert.h>
#include <linux/tls.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gnutls/gnutls.h>

static int sock_bkp = -1;

static void ktls_init(int sock, gnutls_session_t session) {
    int            rc;
    gnutls_datum_t mac_key, nonce, key;
    uint8_t        seq_no[8];

    gnutls_cipher_algorithm_t cipher = gnutls_cipher_get(session);
    assert(cipher == GNUTLS_CIPHER_AES_256_GCM);

    gnutls_protocol_t proto = gnutls_protocol_get_version(session);
    assert(proto == GNUTLS_TLS1_3);

    if ((rc = gnutls_record_get_state(session, 0, &mac_key, &nonce, &key,
                                      seq_no)) < 0) {
        fprintf(stderr, "record_get_state: %s\n", gnutls_strerror(rc));
        exit(EXIT_FAILURE);
    }

    // See https://www.kernel.org/doc/html/latest/networking/tls.html.
    struct tls12_crypto_info_aes_gcm_256 info;
    memset(&info, 0, sizeof(info));

    info.info.version     = TLS_1_3_VERSION;
    info.info.cipher_type = TLS_CIPHER_AES_GCM_256;

    // See RFC 5288.

    // Here, IV refers to the _explicit_ IV, which, per RFC 5288 § 3, is the
    // second part of the nonce.
    assert(TLS_CIPHER_AES_GCM_256_IV_SIZE ==
           nonce.size - TLS_CIPHER_AES_GCM_256_SALT_SIZE);
    memcpy(info.iv, nonce.data + TLS_CIPHER_AES_GCM_256_SALT_SIZE,
           TLS_CIPHER_AES_GCM_256_IV_SIZE);

    _Static_assert(TLS_CIPHER_AES_GCM_256_REC_SEQ_SIZE == sizeof(seq_no),
                   "Sequence number size mismatch.");
    memcpy(info.rec_seq, seq_no, TLS_CIPHER_AES_GCM_256_REC_SEQ_SIZE);

    assert(TLS_CIPHER_AES_GCM_256_KEY_SIZE == key.size);
    memcpy(info.key, key.data, TLS_CIPHER_AES_GCM_256_KEY_SIZE);

    // Here, salt refers to the implicit IV, which should be the first four
    // bytes of the nonce. Again, RFC 5288 § 3.
    memcpy(info.salt, nonce.data, TLS_CIPHER_AES_GCM_256_SALT_SIZE);

    if (setsockopt(sock, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) < 0) {
        perror("setsockopt(\"tls\")");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sock, SOL_TLS, TLS_TX, &info, sizeof(info)) < 0) {
        perror("setsockopt(info)");
        exit(EXIT_FAILURE);
    }
}

static void ktls_send_closure_alert(int sock) {
    struct msghdr   msg = { 0 };
    struct cmsghdr* cmsg;
    char            buf[CMSG_SPACE(1)];
    struct iovec    msg_iov;

    msg.msg_control    = buf;
    msg.msg_controllen = sizeof(buf);
    cmsg               = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level   = SOL_TLS;
    cmsg->cmsg_type    = TLS_SET_RECORD_TYPE;
    cmsg->cmsg_len     = CMSG_LEN(1);
    *CMSG_DATA(cmsg)   = 21; // Alert.
    msg.msg_controllen = cmsg->cmsg_len;

    uint8_t alert[2] = { GNUTLS_AL_WARNING, GNUTLS_A_CLOSE_NOTIFY };
    msg_iov.iov_base = alert;
    msg_iov.iov_len  = sizeof(alert);
    msg.msg_iov      = &msg_iov;
    msg.msg_iovlen   = 1;

    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg");
        exit(EXIT_FAILURE);
    }
}

static void ktls_close(int sock, gnutls_session_t session) {
    int                                  rc;
    struct tls12_crypto_info_aes_gcm_256 info;
    socklen_t                            info_length = sizeof(info);

    if (getsockopt(sock, SOL_TLS, TLS_TX, &info, &info_length) < 0) {
        perror("getsockopt");
        exit(EXIT_FAILURE);
    }

    // Restore sequence number.
    if ((rc = gnutls_record_set_state(session, 0, info.rec_seq)) < 0) {
        fprintf(stderr, "record_set_state: %s.\n", gnutls_strerror(rc));
        exit(EXIT_FAILURE);
    }

    // Turn off kTLS.
    memset(&info, 0, sizeof(info));
    info.info.cipher_type = TLS_CIPHER_AES_GCM_256;
    if (setsockopt(sock, SOL_TLS, TLS_TX, &info, sizeof(info)) < 0) {
        perror("setsockopt(!info)");
        exit(EXIT_FAILURE);
    }
}

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

    ktls_init(sock, session);
    const char msg[] = "Hello from kTLS.\n";
    if (send(sock, msg, sizeof(msg), 0) < 0)
        perror("send_ktls");

    printf("Done.\n");

    /*    ktls_close(sock, session);
          gnutls_bye(session, GNUTLS_SHUT_WR);*/
    ktls_send_closure_alert(sock);
    close(sock);
    gnutls_deinit(session);
    close(listen_sock);
    gnutls_certificate_free_credentials(cred);
}
