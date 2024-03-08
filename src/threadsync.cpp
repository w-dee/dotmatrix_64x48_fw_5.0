#include <Arduino.h>
#include <freertos/semphr.h>
#include <threadsync.h>
#include <deque>

// TODO: use FreeRTOS's native queue object
static portMUX_TYPE queue_lock = portMUX_INITIALIZER_UNLOCKED;
struct handler_queue_item_t;
static std::deque<handler_queue_item_t *> queue;

struct handler_queue_item_t
{
    sync_handler_t handler;
    TaskHandle_t waiting_task;
    int retval;

    handler_queue_item_t(sync_handler_t _handler) :
        handler(_handler), waiting_task(xTaskGetCurrentTaskHandle()), retval(-1)
    {
    }

    ~handler_queue_item_t()
    {
    }

    void execute()
    {
        retval = handler();
    }

    void wait_execution_done()
    {
        ulTaskNotifyTake(
                            pdTRUE,          /* Clear the notification value 
                                                before exiting. */
                            portMAX_DELAY ); /* Block indefinitely. */
    }

    void notify_execution_done()
    {
        xTaskNotifyGive(waiting_task);
    }
};


/**
 * run specified handler in main thread
 * */
int run_in_main_thread(sync_handler_t handler)
{
    // create queue item on heap
    handler_queue_item_t * item = new handler_queue_item_t(handler);

    // lock queue and push it back
	portENTER_CRITICAL(&queue_lock);
    queue.push_back(item);
    portEXIT_CRITICAL(&queue_lock);

    // wait for the handler execution done
    // queue item is removed from deque in poll_main_thread_queue() function
    item->wait_execution_done();

    int retval = item->retval;

    // delete handler item
    delete item;

    return retval;
}

/**
 * poll queue for the next handler item, if exist, execute it
 * */
void poll_main_thread_queue()
{
    handler_queue_item_t *item = nullptr;
	portENTER_CRITICAL(&queue_lock);
    if(queue.size() > 0)
    {
        item = queue.front();
        queue.pop_front();
    }
    portEXIT_CRITICAL(&queue_lock);

    if(!item) return;

    // run the handler
    item->execute();

    // tell waiting task that the handler has done
    item->notify_execution_done();
}