#include <Arduino.h>

#include "pendulum.h"


pendulum_t::pendulum_t(pendulum_t::callback_t _callback, uint32_t interval_ms) :
	callback(_callback)
{
	timer = xTimerCreate("pendulum", pdMS_TO_TICKS(interval_ms), pdTRUE, this, callback_fn);
	// TODO: panic if the timer allocation has failed
	while(pdPASS != xTimerStart(timer, 0)); // do we need to put it into a loop?
}

pendulum_t::~pendulum_t()
{
	while(pdPASS != xTimerStop(timer, 0)); // is this needed ?
	while(pdPASS != xTimerDelete(timer, 0));
}

void pendulum_t::callback_fn(TimerHandle_t xTimer )
{
	pendulum_t *p = reinterpret_cast<pendulum_t*> (pvTimerGetTimerID(xTimer));
	p->callback();
}

