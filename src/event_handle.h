#pragma once

#include <stdbool.h>

#include <a3/sll.h>

#include "forward.h"

typedef SLL(Event) EventQueue;

void event_queue_init(EventQueue*);
void event_handle(Event*, struct io_uring*);
void event_handle_all(EventQueue*, struct io_uring*);
