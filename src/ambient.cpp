#include <Arduino.h>
#include "ambient.h"
#include "interval.h"
#include <rom/crc.h>
#include "matrix_drive.h"
#include "settings.h"
#include "status_led.h"

// Ambient sensor handling

#define ADC_NUM 39 // ambient sensor number

// read raw ambient value
static uint16_t _read_ambient()
{
    return analogRead(ADC_NUM);
}




// So, what should we do to implement LED brightness responsive to ambient light brightness.
// ^
// | LED brightness                                      -----*
// |                                                 ++++      (max)
// |                                             ++++
// |                                  -----*-----
// |                               +++
// |                            +++
// |              -----*-----+++
// |            ++
// |          ++                      * = set point
// |   *-----
// |  (min)
// +----------------------------------------------------------------------> ambient brightness
//
// * Brightness response curve must be monotonic.
// * Ambient brightness setpoints have some margin at left(lower ambient) and right(higher ambient)
// * Brightness between setpoints are linearly interpolated.
// * Non-monotonic or no-significant setpoints are removed.
// * Number of total setpoints are maintained within 16.




static constexpr int16_t AMBIENT_MAX = 1024;
static constexpr int16_t BRIGHTNESS_MAX = 256;
static constexpr int16_t DEFAULT_BRIGHTNESS = LED_CURRENT_GAIN_MAX / 2;
static constexpr int16_t INVALID_AMBIENT = -1;
static constexpr int16_t AMBIENT_MARGIN = 40;

struct setpoint_t
{
    int16_t ambient;
    int16_t brightness;
};

static int compare_setpoint_t(const void *a, const void *b)
{
    return ((const setpoint_t *)a)->ambient - ((const setpoint_t *)b)->ambient;
}


static constexpr int MAX_SETPOINTS = 16;

static setpoint_t setpoints[MAX_SETPOINTS];

// initialize the setpoints to the default
static void init_setpoints()
{
    // make at least two setpoints, that is minimum and maximum
    for(auto && n : setpoints) { n.ambient = INVALID_AMBIENT; n.brightness = 0; }

    setpoints[0] = { .ambient  = 0, .brightness = DEFAULT_BRIGHTNESS }; 
    setpoints[MAX_SETPOINTS - 1] = { .ambient = AMBIENT_MAX, .brightness = DEFAULT_BRIGHTNESS };
}


// attempt to insert a setpoint.
static void _insert_setpoint(setpoint_t sp)
{
    // clamp
    if(sp.ambient < 0) sp.ambient = 0;
    if(sp.ambient > AMBIENT_MAX) sp.ambient = AMBIENT_MAX;
    if(sp.brightness < 0) sp.brightness = 0;
    if(sp.brightness > BRIGHTNESS_MAX) sp.brightness = BRIGHTNESS_MAX;


    // backup a copy
    setpoint_t bk[MAX_SETPOINTS];
    memcpy(bk, setpoints, sizeof(setpoints));

    // erase non-monotonic points
    int i;
    for(i = 0; i < MAX_SETPOINTS; ++i)
    {
        if(bk[i].ambient == INVALID_AMBIENT) continue;
        if((bk[i].ambient < sp.ambient && bk[i].brightness > sp.brightness) ||
           (bk[i].ambient > sp.ambient && bk[i].brightness < sp.brightness))
            bk[i].ambient = INVALID_AMBIENT;
    }

    // erase existing overlapping points
    for(i = 0; i < MAX_SETPOINTS; ++i)
    {
        if(bk[i].ambient == INVALID_AMBIENT) continue;
        if(
                bk[i].ambient - AMBIENT_MARGIN <= sp.ambient &&
                bk[i].ambient + AMBIENT_MARGIN >= sp.ambient)
        {
            bk[i].ambient = INVALID_AMBIENT;
        }
    }

    // insert new point
    for(i = 0; i < MAX_SETPOINTS; ++i)
    {
        if(bk[i].ambient == INVALID_AMBIENT)
        {
            bk[i] = sp;
            break;
        }
    }
    if(i == MAX_SETPOINTS)
    {
        // no slot available ...
        printf("ambient: NO SLOT\n");
        return;
    }

    // if leftmost and rightmost has gone, let revive
    bool found;
    found = false;
    for(auto &&n : bk) { if(n.ambient == 0) { found = true; break; } }
    if(!found)
    {
        int16_t min = BRIGHTNESS_MAX;
        for(auto &&n : bk) { if(n.ambient == INVALID_AMBIENT) continue; if(n.brightness < min) min = n.brightness; }
        for(auto &&n : bk) { if(n.ambient == INVALID_AMBIENT) { n.ambient = 0; n.brightness = min; break; } }
    }

    found = false;
    for(auto &&n : bk) { if(n.ambient == AMBIENT_MAX) { found = true; break; } }
    if(!found)
    {
        int16_t max = 0;
        for(auto &&n : bk) { if(n.ambient == INVALID_AMBIENT) continue; if(n.brightness > max) max = n.brightness; }
        for(auto &&n : bk) { if(n.ambient == INVALID_AMBIENT) { n.ambient = AMBIENT_MAX; n.brightness = max; break; } }
    }


    // sort
    qsort(bk, sizeof(bk)/sizeof(bk[0]), sizeof(bk[0]), compare_setpoint_t);

    // copyback
    for(auto && n : setpoints) n = {INVALID_AMBIENT, 0};
    int wp = 0;
    for(i = 0; i < MAX_SETPOINTS; ++i)
    {
        if(bk[i].ambient == INVALID_AMBIENT) continue;
        if(bk[i].ambient == AMBIENT_MAX) // end
        {
            setpoints[MAX_SETPOINTS - 1] = bk[i];
            break;
        }
        setpoints[wp++] = bk[i];
    }

    // I'm tired. let me go home.
    return;
}


// insert setpoint, by two value
void ambient_insert_setpoint(int16_t ambient, int16_t brightness)
{
    _insert_setpoint({ambient, brightness});
}

// Dump setpoints memory
void ambient_dump()
{
    for(int i = 0; i < MAX_SETPOINTS; ++i)
    {
        printf("%d: %4d %4d\n", i, setpoints[i].ambient, setpoints[i].brightness);
    }
}

// write settings to the settings store
static void write_ambient_settings()
{
	// make string vector representing the array
	string_vector vec;
	for(int i = 0; i < MAX_SETPOINTS; ++i)
		vec.push_back(String((int)setpoints[i].ambient) + "," + String((int)setpoints[i].brightness));

	// write
	settings_write_vector("ambient", vec, SETTINGS_OVERWRITE);
}

// load settings from the setgins store
static void read_ambient_settings()
{
	string_vector vec;
	if(!settings_read_vector("ambient", vec))
	{
		printf("ambient: ambient settings not found\n");
		return; // not yet stored
	}
	if(vec.size() != MAX_SETPOINTS)
	{
		// invalid data
		printf("ambient: corrupted data: data number mismatch\n");
		return;
	}
	setpoint_t ar[MAX_SETPOINTS];
	for(int i = 0; i < MAX_SETPOINTS; ++i)
	{
		String s = vec[i];
		int amb = 0, bri = 0;
		if(2 != sscanf(s.c_str(), "%d,%d", &amb, &bri))
		{
			printf("ambient: corrupted data: %s is not parsable\n", s.c_str());
			return;
		}
		if(amb < INVALID_AMBIENT || amb > AMBIENT_MAX ||
			bri < 0 || bri > BRIGHTNESS_MAX)
		{
			printf("ambient: corrupted data: %s: value out of range\n", s.c_str());
			return; // data out of range
		}
		ar[i] = {(int16_t)amb, (int16_t)bri};
	}

	// all read.
	memcpy(setpoints, ar, sizeof(ar));
}


// get ambient range at the given setpoint index
static void get_ambient_range(int i, int16_t *min, int16_t *max)
{
    bool right_left_point = (i == 0 || i == MAX_SETPOINTS - 1);
    if(min) *min = right_left_point ? setpoints[i].ambient : setpoints[i].ambient - AMBIENT_MARGIN;
    if(max) *max = right_left_point ? setpoints[i].ambient : setpoints[i].ambient + AMBIENT_MARGIN;
}

// convert ambient to brightness
int16_t ambient_to_brightness(int16_t ambient)
{
    // locate the parameter in setpoints[]
    for(int i = 0; i < MAX_SETPOINTS; ++i)
    {
        if(setpoints[i].ambient == INVALID_AMBIENT) continue;
        int16_t min = 0, max = 0;
        get_ambient_range(i, &min, &max);
        if(ambient < min)
        {
            if(i == 0) return 0;
            // interpolate
            int16_t left_max;
            int left = i -1;
            while(setpoints[left].ambient == INVALID_AMBIENT && left >= 0) --left;
            if(left < 0)
            {
                // never. never!
                printf("ambient: logic broken: no left point.\n");
                return DEFAULT_BRIGHTNESS;
            }
            get_ambient_range(left, nullptr, &left_max);
            long width = min - left_max;
            if(width <= 0)
            {
                // ?? no margin to interpolate
                return setpoints[i].brightness;
            }
            int16_t p =
                (int32_t)(setpoints[i].brightness - setpoints[left].brightness) * (ambient - left_max) / width +
                setpoints[left].brightness;
            return p;
        }

        if(ambient <= max)
            return setpoints[i].brightness;
    }

    // never.
    printf("ambient: logic broken: interpolation failed.\n");
    return DEFAULT_BRIGHTNESS;
}

static int16_t last_read_ambient;
static uint32_t freeze_ambient_until;
static bool ambient_freezing;
static bool ambient_ledtest_fix_brightness; // always full brightness for led test
static int ambient_ledtest_fixed_brightness; // the fixed brightness value index
static int ambient_brightness_by_current_ambient; // brightness value index from current ambient; not affected by ambient_ledtest_fix_brightness
static constexpr int AMBIENT_STEP_PREC = 8192;
static int current_ambient_brightness = DEFAULT_BRIGHTNESS * AMBIENT_STEP_PREC; // current ambient brightness used by adaptive smoothing; multiplied by AMBIENT_STEP_PREC
static int current_ambient_brightness_step; // current ambient brightness incremental smoothing factor
static constexpr int MAX_AMBIENT_BRIGHTNESS_STEP = AMBIENT_STEP_PREC/2;
static constexpr int AMBIENT_FAST_STEP_THRESH = 50; // above this difference, ambient steo goes at max speed
static int16_t read_ambient()
{
	// the ambient raw value at my development desk is:
	// 820 --- almost complete dark
	// 500 --- under strong light

	// According to the PhotoTransistor(PT)=NJL7302-F3's datasheet,
	// light current vs illuminance is almost linear,
	// IL = Ev*a + b
	// where a = 0.5, b =~ 0

	// the circuit measuring the ambient light is:

	//    +  +3.3V
	//    |
	//    <
	//    >   32k
	//    <
	//    |
	//    *------------> to ADC
	//    |
	//    *--------+
	//    |        |
	//    <        C
	//    >        >|  (NJL7302L-F3 PT)
	//    < 10k    E
	//    |        |
	//    *--------+
	//    |
	//  ----- GND

	// Vce (ie. ADC voltage) =
	// Vce = IL * -7618.98956960328 + 0.785708299365338
	// IL = (Vce - 0.785708299365338) / -7618.98956960328 
	static int32_t lpf_value;
	int32_t raw = _read_ambient() << 8; // '<<8' to increase integer arithmetic precision
	lpf_value += (raw - lpf_value) >> 4;

	if(!ambient_freezing)
	{
		float il = (lpf_value * (1.0 / 1000.0 / (float)(1<<8)) - 0.785708299365338) * (1.0 / -7618.98956960328 );
		int lv = (int)(20000000.0 * il) + 50; // reformat to easy-to-handle range
		if(lv < 0) lv = 0;
		if(lv > AMBIENT_MAX) lv = AMBIENT_MAX;
		last_read_ambient = lv;
	}
	else
	{
		if((int32_t)(freeze_ambient_until - millis()) <= 0)
		{
			if(ambient_freezing) write_ambient_settings();
			ambient_freezing = false;
		}
	}

	return last_read_ambient;
}



void init_ambient(void)
{
    init_setpoints();
	read_ambient_settings();
}

static void set_brightness_from_ambient(bool force = false)
{
    int index;

    int16_t ambient = last_read_ambient;
    int ambient_index = ambient_to_brightness(ambient);

    // adaptive brightness smoothing
    int target = (ambient_index * AMBIENT_STEP_PREC) + AMBIENT_STEP_PREC/2;
    EVERY_MS(20)
    {
        // note that this function is called shorter interval than 200ms;
        // but smoothing should be constant speed
        bool sign = target > current_ambient_brightness;
        if((current_ambient_brightness_step > 0) != sign)
        {
            // sign has changed;
            // reset current ambient brightness step 
            current_ambient_brightness_step = sign ? 1 : -1;
        }
        else if(target - current_ambient_brightness < -AMBIENT_FAST_STEP_THRESH*AMBIENT_STEP_PREC ||
                target - current_ambient_brightness > AMBIENT_FAST_STEP_THRESH*AMBIENT_STEP_PREC)
        {
            current_ambient_brightness_step = sign ? MAX_AMBIENT_BRIGHTNESS_STEP : -MAX_AMBIENT_BRIGHTNESS_STEP;
        }
        else
        {
            // sigh has not changed;
            // make step progressive
            if(current_ambient_brightness_step < 0)
            {
                current_ambient_brightness_step --;
                if(current_ambient_brightness_step < -MAX_AMBIENT_BRIGHTNESS_STEP) current_ambient_brightness_step = -MAX_AMBIENT_BRIGHTNESS_STEP;
            }
            else
            {
                current_ambient_brightness_step ++;
               if(current_ambient_brightness_step > MAX_AMBIENT_BRIGHTNESS_STEP) current_ambient_brightness_step = MAX_AMBIENT_BRIGHTNESS_STEP;
            }
        }
        // increment current_ambient_brightness by the step
        if(target > current_ambient_brightness)
        {
            current_ambient_brightness += current_ambient_brightness_step;
            if(current_ambient_brightness > target)
                current_ambient_brightness = target, current_ambient_brightness_step = 0; // also reset current_ambient_brightness_step
        }
        else if(target < current_ambient_brightness)
        {
            current_ambient_brightness += current_ambient_brightness_step;
            if(current_ambient_brightness < target)
                current_ambient_brightness = target, current_ambient_brightness_step = 0; // also reset current_ambient_brightness_step
        }
        else
            current_ambient_brightness_step = 0; // reset
//        printf("ambient: %d, brightness: %d, target: %d, step: %d, reduced: %d\n",
//            ambient, current_ambient_brightness, target, current_ambient_brightness_step, current_ambient_brightness / AMBIENT_STEP_PREC);
    }
    END_EVERY_MS
    if(force) current_ambient_brightness = target;
    index = ambient_brightness_by_current_ambient = current_ambient_brightness / AMBIENT_STEP_PREC;

    if(ambient_ledtest_fix_brightness)
    {
        index = ambient_ledtest_fixed_brightness; // override
    }

    matrix_drive_set_current_gain(index);

    // index 0 is a special value which blanks the main clock display;
    // but do not blank the status LEDs at even lowest brightness.
    // map brightness index to 10 ... 256
    int v = (256 - 10) * index / LED_CURRENT_GAIN_MAX + 10;
    status_led_set_global_brightness(v);
}

int sensors_get_brightness_by_current_ambient()
{
    return ambient_brightness_by_current_ambient;
}


void poll_ambient()
{

    EVERY_MS(20)
    {
        read_ambient();
        set_brightness_from_ambient();
    }
    END_EVERY_MS

}

int16_t get_ambient()
{
	return last_read_ambient;
}


#define AMBIENT_FREEZE_TIME 5000 // ambient brightness reading freezing time in ms
// freezing ambient for certain time period. to 
// ease brightness setting user interaction.
void freeze_ambient()
{
	ambient_freezing = true;
	freeze_ambient_until = millis() + AMBIENT_FREEZE_TIME;
}

void sensors_set_brightness_always_max(bool b)
{
    ambient_ledtest_fix_brightness = b;
    ambient_ledtest_fixed_brightness = LED_CURRENT_GAIN_MAX;
    set_brightness_from_ambient();
}

/**
 * Fix brightness at specified value index.
 * pass -1 to restore default behaviour
 * */
void sensors_set_brightness_fix(int brightness)
{
    if(brightness >= 0)
    {
        ambient_ledtest_fix_brightness = true;
        ambient_ledtest_fixed_brightness = brightness;
    }
    else
    {
        ambient_ledtest_fix_brightness = false;
    }
    set_brightness_from_ambient();
}

void sensors_change_current_brightness(int amount)
{
	freeze_ambient();
	int16_t ambient = read_ambient();
	int index = ambient_to_brightness(ambient);
	index += amount;
	if(index < 0) index = 0;
	else if(index > LED_CURRENT_GAIN_MAX) index = LED_CURRENT_GAIN_MAX;
	printf("ambient: New current gain %d at brightness %d\n", index, (int)ambient);
	ambient_insert_setpoint(ambient, index);
	//ambient_dump();
    set_brightness_from_ambient(true);
}