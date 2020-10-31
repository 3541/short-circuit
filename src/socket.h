#pragma once

#include <netinet/in.h>

#include "types.h"

fd socket_listen(in_port_t port);
