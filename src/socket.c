#include "socket.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>

#include <a3/util.h>

#include "config.h"

fd socket_listen(in_port_t port) {
    fd ret;
    UNWRAPS(ret, socket(AF_INET, SOCK_STREAM, 0));

    const int enable = 1;
    UNWRAPSD(
        setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    UNWRAPSD(
        setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    UNWRAPSD(bind(ret, (struct sockaddr*)&addr, sizeof(addr)));
    UNWRAPSD(listen(ret, LISTEN_BACKLOG));

    return ret;
}
