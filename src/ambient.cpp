#include <Arduino.h>
#include "ambient.h"
#include "interval.h"
#include <rom/crc.h>

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
static uint16_t read_ambient()
{
    return analogRead(ADC_NUM);
}






static int32_t average = 0; // binarizer threshold value (deeply averaged value)
static int32_t lpf = 0;
#if 0
static uint8_t hist[5]; // histgram for tracking synchronization
static constexpr size_t NUM_HIST = sizeof(hist) / sizeof(hist[0]);
static uint8_t hist_phase; // histgram phase
static uint8_t hist_locked_phase; // found clock-invarint phase of the histgram
static bool clock_found; // whether clock signal is found or not
static bool prev_value; // previous detected binarized value
static constexpr uint8_t HIST_MAX = 20; // maximum histgram value
static constexpr uint8_t HIST_MIN = 1; // maximum histgram's valley which should found in clock synchronization
#endif

// do sampling by certain period interval(currently 20ms)
static void do_sample()
{
	// compute average value
	int32_t re = (int32_t)read_ambient() << 8;
	lpf += (re - lpf) >> 1;

	average += (lpf - average) >> 8;
#if 0
	// make histgram
	bool bit = lpf < average;
	if(prev_value != bit) hist[hist_phase] ++;
	prev_value = bit;

	// check data if there is a clock to synchronize
	if(clock_found && hist_phase == hist_locked_phase)
	{
		state.push_bit(bit);
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
		for(int i = 0 ; i < NUM_HIST; ++i) 
		{
//			printf("%d ", hist[i]);
			if(hist[i] <= min)
			{
				min = hist[i];
				found_phase_top = i;
			}
			if(hist[i] >= HIST_MAX) clear = true;
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
#endif
}




void poll_ambient()
{
    EVERY_MS(20)
    {
        do_sample();
    }
    END_EVERY_MS
}

uint32_t get_ambient()
{
	return average;
}