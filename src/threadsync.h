#pragma once


#include <functional>

typedef std::function<int(void)> sync_handler_t;

int run_in_main_thread(sync_handler_t handler);
void poll_main_thread_queue();

