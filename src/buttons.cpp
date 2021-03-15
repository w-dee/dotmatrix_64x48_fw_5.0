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

static bool phys_button_disabled = false; // whether the physical button input is disabled or not

#define BUTTON_DEBOUNCE_COUNT 4
#define BUTTON_INITIAL_REPEAT_DELAY 50
#define BUTTON_REPEAT_LIMIT 56


/**
 * button update handler called approx. 10ms
 */
static void button_update_handler()
{
	auto br = matrix_button_scan_bits; // button_read is updated in matrix_drive.cpp
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

uint32_t button_get_scan_bits()
{
	if(phys_button_disabled) return 0;
	return matrix_button_scan_bits;
}

/**
 * Check whether the physical buttons are all pressed. 
 * Then, disable the physical button input. This will happen
 * when the button input is floating. */
void button_check_physical_buttons_are_sane()
{
	printf("Buttons: checking whether the physical button input is properly connected ... button scan state : %02x\n",
		matrix_button_scan_bits);
	if((matrix_button_scan_bits & ((1<<MAX_BUTTONS)-1)) == ((1<<MAX_BUTTONS)-1))
	{
		printf("Buttons: No. disabling physical button input.\n");
		phys_button_disabled = true;
	}
	else
	{
		printf("Buttons: Yes.\n");
	}
}

void button_update()
{
	if(!phys_button_disabled)
	{
		EVERY_MS(10)
		{
			button_update_handler();
		}
		END_EVERY_MS
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

