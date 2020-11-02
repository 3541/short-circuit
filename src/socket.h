#pragma once

#include <netinet/in.h>

typedef int fd;

fd socket_listen(in_port_t port);
