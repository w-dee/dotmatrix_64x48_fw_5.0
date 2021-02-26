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
    SemaphoreHandle_t runsem;
    int retval;

    handler_queue_item_t(sync_handler_t _handler) :
        handler(_handler), runsem(xSemaphoreCreateMutex()), retval(-1)
    {
        xSemaphoreTake(runsem, portMAX_DELAY);
    }

    ~handler_queue_item_t()
    {
        vSemaphoreDelete(runsem);
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
    // queue item is removed in anothor function
    xSemaphoreTake(item->runsem, portMAX_DELAY);
    xSemaphoreGive(item->runsem);

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
    item->retval = item->handler();

    // release semaphore to tell waiting thread that the handler has done
    xSemaphoreGive(item->runsem);
}