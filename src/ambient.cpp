#include <Arduino.h>
#include "ambient.h"
#include "interval.h"
#include <rom/crc.h>
#include "matrix_drive.h"

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
static constexpr int16_t AMBIENT_MARGIN = 64;

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
	lpf_value += (raw - lpf_value) >> 3;

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
			ambient_freezing = false;
		}
	}

	return last_read_ambient;
}



void init_ambient(void)
{
    init_setpoints();
}


void poll_ambient()
{

    EVERY_MS(200)
    {
		int16_t ambient = read_ambient();
		int index = ambient_to_brightness(ambient);
		matrix_drive_set_current_gain(index);
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

void sensors_set_contrast_always_max(bool b)
{
	// TODO: implement this
}

void sensors_change_current_contrast(int amount)
{
	freeze_ambient();
	int16_t ambient = read_ambient();
	int index = ambient_to_brightness(ambient);
	index += amount;
	if(index < 0) index = 0;
	else if(index > LED_CURRENT_GAIN_MAX) index = LED_CURRENT_GAIN_MAX;
	printf("ambient: New current gain %d at brightness %d\n", index, (int)ambient);
	ambient_insert_setpoint(ambient, index);
	ambient_dump();
	matrix_drive_set_current_gain(index);
}