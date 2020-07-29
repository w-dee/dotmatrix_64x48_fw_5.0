#pragma once


#include <functional>

typedef std::function<void(void)> sync_handler_t;

void run_in_main_thread(sync_handler_t handler);
void poll_main_thread_queue();

