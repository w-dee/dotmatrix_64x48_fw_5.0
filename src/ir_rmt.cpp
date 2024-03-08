#include <Arduino.h>
#include <driver/rmt.h>
#include "ir_rmt.h"

#define RMT_RX_PIN 36
#define RMT_TX_PIN 12
#define RMT_MODULATION_HZ 37800u // IR modulation frequency. approx. 38kHz.
            // Not sure why some documents denote a bit lower frequency.
#define RAW_BUF_LENGTH 1024 // in rmt_item32_t's count
#define RMT_CLK_DIV 160  // 2us resolution ... should be enough
#define RMT_FILTER_THRESH 50
#define RMT_FILTER_TIMEOUT 65000 // some remote controller needs longer timeout like this

const char rmt_save_prefix[] = "/settings/rmt/";




void init_rmt()
{
    
}



// use static buffer to receive and transmit
static rmt_item32_t items[RAW_BUF_LENGTH];
static uint32_t item_count;
static volatile rmt_result_t result; // last receive result
    // in real NUMA systems, inter-task(inter-process) communication
    // muse use proper synchronization items like mutex or semaphore;
    // but we are now in ESP32, which one CPU can see a value
    // immediately after another CPU wrote it. don't worry about it.

static void receive_task(void *)
{
    // configure rmt receiver and install the driver
    rmt_config_t rmt_rx_config;
    memset(&rmt_rx_config, 0, sizeof(rmt_rx_config));
    rmt_rx_config.rmt_mode = RMT_MODE_RX;
    rmt_rx_config.channel = RMT_CHANNEL_0;
    rmt_rx_config.clk_div = RMT_CLK_DIV;
    rmt_rx_config.gpio_num = (gpio_num_t)RMT_RX_PIN;
    rmt_rx_config.mem_block_num = 1;
    //rmt_rx_config.flags = 0;
    rmt_rx_config.rx_config.idle_threshold = RMT_FILTER_TIMEOUT;
    rmt_rx_config.rx_config.filter_ticks_thresh = RMT_FILTER_THRESH;
    rmt_rx_config.rx_config.filter_en = true;
    rmt_config(&rmt_rx_config);
    rmt_driver_install(RMT_CHANNEL_0, 1000, 0);

    // get ring buffer handle
    RingbufHandle_t rb = nullptr;
    rmt_get_ringbuf_handle(RMT_CHANNEL_0, &rb);

    // start receive
    size_t count = 0;
    bool first = true;
    while(true)
    {
        rmt_rx_start(RMT_CHANNEL_0, first);
        while(rb)
        {
            if(result == rmt_interrupting) goto done;
            rmt_item32_t * ring_items = nullptr;
            size_t length = 0;
            ring_items = (rmt_item32_t *) xRingbufferReceive(rb, &length, 1000);
            if(ring_items)
            {
                length /= 4; // one RMT item = 4bytes
                // do copy
                if(count + length > RAW_BUF_LENGTH)
                {
                    result = rmt_nomem; // buffer overflow
                    goto done;
                }
                memcpy(items + count, ring_items, length * 4);
                count += length;
                // return the buffer to the ring buffer
                vRingbufferReturnItem(rb, (void*) ring_items);
            }
            else
            {
                break;
            }
        }
        first = false;
    }

done:
    rmt_rx_stop(RMT_CHANNEL_0);

    rmt_driver_uninstall(RMT_CHANNEL_0);
    item_count = count;

    if(result == rmt_rx) result = rmt_done;
    else if(result == rmt_interrupting) result = rmt_interrupted;

    vTaskDelete(NULL); // suicide
}

void rmt_start_receive()
{
    if(result != rmt_idle) return; // other rmt function is running

    result = rmt_rx;

    xTaskCreate(
        receive_task,           /* Task function. */
        "RMT RX Task",        /* name of task. */
        8192,                    /* Stack size of task */
        (void *)0,                     /* parameter of the task */
        1,                        /* priority of the task */
        nullptr); /* Task handle to keep track of created task */
}


void rmt_stop_receive()
{
    result = rmt_interrupting;
    while(result == rmt_interrupting)
    {
        vTaskDelay(portTICK_PERIOD_MS * 10); // wait for a while
    }
}

bool rmt_in_progress()
{
    switch(result)
    {
    case rmt_interrupting: // interrupting by request
    case rmt_rx: // rx in progress
    case rmt_tx: // tx in progress
        return true;

    case rmt_idle: // idle
    case rmt_done: // success
    case rmt_interrupted: // interrupted by request
    case rmt_nomem: // buffer overflow
    case rmt_broken:
    case rmt_notfound:
        return false;
    }
    return false;
}

void rmt_wait()
{
    while(rmt_in_progress())
    {
        vTaskDelay(portTICK_PERIOD_MS * 10); // wait for a while
    }
}

void rmt_save(const String & filename)
{
    // TODO: check available conf space 
    if(result != rmt_done) return; // nothing to save
    FILE * f = fopen((rmt_save_prefix + filename).c_str(), "wb");
    fwrite(items, sizeof(items[0]), item_count, f); // TODO: error handling
    fclose(f);
    rmt_clear();
}

void rmt_clear()
{
    item_count = 0;
    result = rmt_idle;
}


static void send_task(void *)
{
    rmt_config_t rmt_tx_config;
    memset(&rmt_tx_config, 0, sizeof(rmt_tx_config));
    rmt_tx_config.rmt_mode = RMT_MODE_TX;
    rmt_tx_config.channel = RMT_CHANNEL_0;
    rmt_tx_config.clk_div = RMT_CLK_DIV;
    rmt_tx_config.gpio_num = (gpio_num_t)RMT_TX_PIN;
    rmt_tx_config.mem_block_num = 1;
    //rmt_rx_config.flags = 0;
    rmt_tx_config.tx_config.carrier_duty_percent = 10;
        // Usually the IR transmitter's modulation pulse duty is 33%;
        // But the hardware underlying now 
        // is slow on recovering transition from HIGH to LOW; So 10% is a good value.
    rmt_tx_config.tx_config.carrier_en = true;
    rmt_tx_config.tx_config.carrier_freq_hz = RMT_MODULATION_HZ;
    rmt_tx_config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
    rmt_tx_config.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
    rmt_tx_config.tx_config.idle_output_en = true;
    rmt_tx_config.tx_config.loop_en = false;
    rmt_config(&rmt_tx_config);
    rmt_driver_install(RMT_CHANNEL_0, 0, 0);
    
    rmt_write_items(RMT_CHANNEL_0, items, item_count, true);

    rmt_driver_uninstall(RMT_CHANNEL_0);

    result = rmt_idle;
    vTaskDelete(NULL); // suicide
}


void rmt_start_send(const String & filename)
{
    if(result != rmt_idle) return; // other rmt function is running

    // open the file and read content
    FILE *f = fopen((rmt_save_prefix + filename).c_str(), "rb");
    if(!f)
    {
        result = rmt_notfound;
        return;
    }
    size_t n = fread(items, sizeof(items[0]), RAW_BUF_LENGTH, f);
    fclose(f);

    // check the last item has "0" duration
    if(n == 0) return; // nothing to send
    if(items[n-1].duration0 != 0 && items[n-1].duration1 != 0)
    {
        // broken data
        result = rmt_broken;
        return;
    }

    // create a task to send
    result = rmt_tx;

    xTaskCreate(
        send_task,           /* Task function. */
        "RMT TX Task",        /* name of task. */
        8192,                    /* Stack size of task */
        (void *)0,                     /* parameter of the task */
        1,                        /* priority of the task */
        nullptr); /* Task handle to keep track of created task */
}

rmt_result_t rmt_get_status()
{
    return result;
}