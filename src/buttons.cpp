#include <Arduino.h>
#include "buttons.h"
#include "matrix_drive.h"
#include "interval.h"

uint8_t buttons[MAX_BUTTONS] = {0};

/**
 * Button repeat / debounce counter.
 * This counters are initially zero.
 * While the button is pused, it counts up.
 * When the repeat counter reaches BUTTON_DEBOUNCE_COUNT,
 * the button is recognized as pushed.
 * The counter continues counting while the button is
 * physically pushed, When the counter reaches BUTTON_INITIAL_REPEAT_DELAY,
 * the button is recognized as pushed again (auto repeat).
 * The counter still continues as long as the button is physically pushed,
 * but when it reaches BUTTON_REPEAT_LIMIT, it becomes back to
 * BUTTON_INITIAL_REPEAT_DELAY,
 * thus recognized again and again as pushed (repeating).
 */
static uint8_t button_debounce_counter[MAX_BUTTONS] = {0};

#define BUTTON_DEBOUNCE_COUNT 4
#define BUTTON_INITIAL_REPEAT_DELAY 50
#define BUTTON_REPEAT_LIMIT 56


/**
 * button update handler called approx. 10ms
 */
static void button_update_handler()
{
	auto br = button_scan_bits; // button_read is updated in matrix_drive.cpp
	for(int i = 0; i < MAX_BUTTONS; i++)
	{
		if(br & (1U << i))
		{
			// physical button pressed
			uint8_t count = button_debounce_counter[i];
			count ++;
			if(count == BUTTON_DEBOUNCE_COUNT)
			{
				if(buttons[i] < 255) buttons[i] ++;
			}
			else if(count == BUTTON_REPEAT_LIMIT)
			{
				count = BUTTON_INITIAL_REPEAT_DELAY;
			}

			if(count == BUTTON_INITIAL_REPEAT_DELAY)
			{
				if(buttons[i] < 255) buttons[i] ++;
			}
			button_debounce_counter[i] = count;
		}
		else
		{
			// physical button released
			button_debounce_counter[i] = 0;
		}
	}
}

void button_update()
{
	EVERY_MS(10)
	{
		button_update_handler();
	}
	END_EVERY_MS
	uint32_t buttons = button_get();
	if(buttons)
	{
		printf("Button: %x\r\n", buttons);
	}
}

uint32_t button_get()
{
	uint32_t ret = 0;
	for(int i = 0; i < MAX_BUTTONS; i++)
	{
		if(buttons[i]) ret |= (1<<i), buttons[i] = 0;
	}
	return ret;
}


void button_push(uint32_t button)
{
	for(int i = 0; i < MAX_BUTTONS; i++)
	{
		if(((1<<i) & button) && buttons[i] != 255) ++buttons[i];
	}
}

