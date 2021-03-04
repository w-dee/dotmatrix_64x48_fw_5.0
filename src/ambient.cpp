#include <Arduino.h>
#include "ambient.h"
#include "interval.h"
#include <rom/crc.h>
#include "matrix_drive.h"

// Ambient sensor handling

#define ADC_NUM 39 // ambient sensor number

#if 0
// unko prng to (hopefully) remove dc bias from the data
struct unko_prng_t
{
	//X ABC Algorithm Random Number Generator for 8-Bit Devices:
	uint8_t x = 0;
	uint8_t a = 0;
	uint8_t b = 0;
	uint8_t c = 0;

	uint8_t operator ()()
	{
		x++;	//x is incremented every round and is not affected by any other variable
		a = (a^c^x);       //note the mix of addition and XOR
		b = (b+a);         //And the use of very few instructions
		c = ((c+(b>>1))^a);  //the right shift is to ensure that high-order bits from b can affect  
		return(c);          //low order bits of other variables
	}
};

static constexpr uint32_t MAGIC = 
	((uint32_t)'M' << 24) + ((uint32_t)'Z' << 16) + ((uint32_t)'5' << 8) + ((uint32_t)'A' << 0);

#pragma pack(push, 1)
// struct for receiving a basic connection information
struct ambient_conn_info_t
{
	char PSK[64];  // maximum PSK length = 63
	uint64_t ts; // timestamp at the time of this structure head
	char SSID[33]; // maximum ssid length = 32
	char padding1[3]; // for future expansion
	uint32_t version; // = 0x100
	uint32_t checksum; // CRC32 checksum of above 

	static uint32_t sum(const uint8_t *buf, size_t n)
	{
		uint16_t a = 0, b = 0;
		for(size_t i = 0; i < n; ++i)
		{
			a += *(buf++);
			a ^= a << 7;
			b += a;
		}
		return a + (b<<16);
	}

	bool check_sum() const
	{
		return checksum == sum((const uint8_t*)this 
			, sizeof(*this) - 4 /* excl. checksum */);
	}

	void clear()
	{
		memset(this, 0, sizeof(*this));
	}

}; // total 116bytes
#pragma pack(pop)

static_assert(sizeof(ambient_conn_info_t) == 116, "struct size is not 116");

struct ambient_conn_state_t
{
	ambient_conn_info_t info;
	uint32_t magic; // magic number
	int bit_pos; // bit writing position
	int byte_pos; // byte writing position
	bool in_sync; // magic number detected

	ambient_conn_state_t(): magic(0), bit_pos(0), byte_pos(0), in_sync(false)
	{
	}

	void push_bit(bool b)
	{
		if(!in_sync)
		{
			// no syncronization pattern found
			magic <<= 1;
			magic |= b?1:0;
			if(magic == MAGIC)
			{
				printf("MZ5ACI: magic found\n");
				in_sync = true;
				info.clear();
			}
		}
		else
		{
			// synchronization pattern found
			uint8_t *p = static_cast<uint8_t *>(static_cast<void *>(&info));
			if(b)
				p[byte_pos] |= (1<<(7-bit_pos));
			else
				p[byte_pos] &= ~(1<<(7-bit_pos));
			++ bit_pos;
			if(bit_pos == 8)
			{
				bit_pos = 0;
				++byte_pos;
				if(byte_pos >= sizeof(info))
				{
					// all data has been received
					printf("MZ5ACI: One record completed.\n");
					printf("MZ5ACI: dump: ");
					for(int i = 0; i < sizeof(info); ++i)
					{
						printf("%02x ", p[i]);
					}
					printf("\n");
					if(info.check_sum())
					{
						printf("MZ5ACI: checksum pass!\n");
						// post-receive process
						printf("MZ5ACI: version: %x\n", info.version);
						printf("MZ5ACI: timestamp: %llu\n", info.ts);
						info.SSID[sizeof(info.SSID)-1] = 0; // force terminate the string
						info.PSK[sizeof(info.PSK)-1]  = 0; // force terminate the string
						printf("MZ5ACI: SSID: %s\n", info.SSID);
						printf("MZ5ACI: PSK: %s\n", info.PSK);
					}

					// reset state
					bit_pos = 0;
					byte_pos = 0;
					in_sync = false;
				}
			}

		}
	}

};

// static store of sampled binarized conn_info 
static ambient_conn_state_t state;

#endif
// read raw ambient value
static uint16_t _read_ambient()
{
    return analogRead(ADC_NUM);
}


#if 0





static int32_t average = 0; // binarizer threshold value (deeply averaged value)
static int32_t bit_average = 0; // binarizer threshold value of non-clocking phase
static int32_t bit_accum = 0; // binarizer accumulator
static uint8_t hist[8]; // histgram for tracking synchronization
static constexpr size_t NUM_HIST = sizeof(hist) / sizeof(hist[0]);
static uint8_t hist_phase; // histgram phase
static uint8_t hist_locked_phase; // found clock-boundary phase of the histgram
static bool clock_found; // whether clock signal is found or not
static bool prev_value; // previous detected binarized value
static constexpr uint8_t HIST_MAX = 20; // maximum histgram value
static constexpr uint8_t HIST_MIN = 5; // maximum histgram's valley which should found in clock synchronization


// do sampling by certain period interval(currently 20ms)
static void do_sample()
{
	// compute average value
	int32_t re = (int32_t)read_ambient() << 8;

	average += (re - average) >> 8;

	// make histgram
	bool bit = re < average;
	if(prev_value != bit) hist[hist_phase] ++;
	prev_value = bit;

	// check data if there is a clock to synchronize
	if(clock_found)
	{
		if(hist_phase == hist_locked_phase)
		{
			// clock changing phase
//			printf("%d %d\n", bit_accum, bit_average);
			bit_average += (bit_accum - bit_average) >> 8;
			state.push_bit(bit_accum < bit_average);
			bit_accum = 0;
		}
		else if(hist_phase != hist_locked_phase -1 &&
				hist_phase != hist_locked_phase +1 &&
				hist_phase -1 != hist_locked_phase &&
				hist_phase +1 != hist_locked_phase )
		{
			// non clock phase
			bit_accum += re;
		}
	}

	// check whether there is clock found
	++hist_phase;
	if(hist_phase>=NUM_HIST)
	{
		hist_phase = 0;
		bool clear = false;
		uint8_t found_phase_top = 0;
//		printf("%d(%d): " , clock_found, hist_locked_phase);
		uint8_t min = 255;
		uint8_t max = 0;
		for(int i = 0 ; i < NUM_HIST; ++i) 
		{
//			printf("%d ", hist[i]);
			if(hist[i] <= min)
			{
				min = hist[i];
			}
			if(hist[i] >= HIST_MAX) clear = true;
			if(hist[i] >= max);
			{
				max = hist[i];
				found_phase_top = i;
			}
		}
//		printf("\n");
		if(clear)
		{
			for(auto && n : hist) n = 0;
			if(min <= HIST_MIN)
			{
				if(!clock_found)
				{
					hist_locked_phase = found_phase_top;
					printf("MZ5ACI: Phase locked.\n");
					clock_found = true;
				}
			}
			else
			{
				if(clock_found)
				{
					printf("MZ5ACI: Phase unlocked.\n");
					clock_found = false;
				}
			}
		}
	}

}


#endif

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