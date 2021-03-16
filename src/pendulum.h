#ifndef PENDULUM_H_
#define PENDULUM_H_

#include <functional>

class pendulum_scheduler_t;

//! Class to call specified function by the specified interval, std::function way.
class pendulum_t
{
public:
	typedef std::function<void ()> callback_t;
	uint32_t interval; //!< interval in ms
	uint32_t next_tick; //!< next event tick

protected:
	callback_t callback;

public: // check pendulum handler does not call blocking functions
	pendulum_t(callback_t _callback, uint32_t _interval_ms);
	~pendulum_t();

private:
	void check(uint32_t check);

	friend class pendulum_scheduler_t;
};

void poll_pendulum();


#endif
