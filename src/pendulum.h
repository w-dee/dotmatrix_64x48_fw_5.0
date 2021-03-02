#ifndef PENDULUM_H_
#define PENDULUM_H_

#include <functional>
#include <freertos/timers.h>

//! Class to call specified function by the specified interval, std::function way.
class pendulum_t
{
public:
	typedef std::function<void ()> callback_t;

protected:
	TimerHandle_t timer;
	callback_t callback;

public: // check pendulum handler does not call blocking functions
	pendulum_t(callback_t _callback, uint32_t interval_ms);
	~pendulum_t();

private:
	static void callback_fn(TimerHandle_t xTimer );
};




#endif
