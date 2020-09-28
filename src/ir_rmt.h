// all functions here are to be called from main thread

void init_rmt(); // initialize rmt subsystem
void rmt_start_receive(); // start rmt receive
void rmt_stop_receive(); // stop(interrupt) rmt receive
bool rmt_in_progress(); // returns whether the RMT processing(RX/TX) is in progress
void rmt_wait(); // wait rmt processing
void rmt_save(const String & filename); // save the rmt buffer
void rmt_clear(); // clear received buffer

void rmt_start_send(const String & filename); // start rmt send


// receive result
enum rmt_result_t
{
    rmt_idle, // idle
    rmt_rx, // rx in progress
    rmt_tx, // tx in progress
    rmt_done, // success
    rmt_interrupting, // interrupting by request
    rmt_interrupted, // interrupted by request
    rmt_nomem, // buffer overflow
    rmt_broken, // broken file
    rmt_notfound, // file not found
};




rmt_result_t rmt_get_status();