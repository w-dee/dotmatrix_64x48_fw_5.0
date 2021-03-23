// original retrieved from https://www.esp32.com/viewtopic.php?t=1743#p8156


#include <soc/io_mux_reg.h>
#include <soc/i2s_reg.h>
#include <soc/i2s_struct.h>
#include "rom/lldesc.h"
#include <WiFi.h>
#include <string.h>

#include "driver/i2s.h"
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"

#include "matrix_drive.h"
#include "frame_buffer.h"
#include "buttons.h"
#include <cmath>


#define IO_PWCLK 19
#define IO_COLCLK 19
#define IO_COLSER 18
#define IO_COLLATCH 5
#define IO_ROWLATCH 32
#define IO_HC595SEROUT 34
#define IO_LED1642_RST 13
#define IO_BUTTONSENSE 35


/**
 * GPIO initialize
 * */
static void led_gpio_init()
{
	pinMode(IO_PWCLK, OUTPUT);
	pinMode(IO_COLCLK, OUTPUT);
	pinMode(IO_COLSER, OUTPUT);
	pinMode(IO_COLLATCH, OUTPUT);
	pinMode(IO_ROWLATCH, OUTPUT);
	pinMode(IO_HC595SEROUT, INPUT);
	pinMode(IO_LED1642_RST, OUTPUT);
	pinMode(IO_BUTTONSENSE, INPUT);

	// note: software reset (SW_CPU_RESET) seems
	// that it *does not* reset the pin matrix assignment.
	// so we will need to reset them at here.
	pinMatrixOutDetach(IO_COLCLK, false, false);
	pinMatrixOutDetach(IO_COLSER, false, false); // bit 0
	pinMatrixOutDetach(IO_COLLATCH, false, false); // bit 1
	pinMatrixOutDetach(IO_ROWLATCH, false, false); // bit 2


}

/**
 * Make any GPIO driving LED1642
 * */
static void led_gpio_set_low()
{
	digitalWrite(IO_PWCLK, LOW);
	digitalWrite(IO_COLCLK, LOW);
	digitalWrite(IO_COLSER, LOW);
	digitalWrite(IO_COLLATCH, LOW);
	digitalWrite(IO_ROWLATCH, LOW);
	digitalWrite(IO_LED1642_RST, LOW);

}


/**
 * reset hard the LED1642 using their VDD shorted to the GND
 * */
static void led_hard_reset_led1642()
{
	led_gpio_set_low();

	// make LED1642's VDD shorted to the GND
	delay(10);

	digitalWrite(IO_LED1642_RST, 1);

	// wait for a while
	delay(100);

	// release VDD
	digitalWrite(IO_LED1642_RST, 0);

	// wait for a while
	delay(10);
}



/**
 * simple lfsr function
 */
static uint32_t led_lfsr(uint32_t lfsr)
{
	lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xd0000001u);
	return lfsr;
}

static void led_print_0_1(int v)
{
	if(v) putchar('1'); else putchar('0');
}

static void led_post()
{
	// prepare for sending bits
	constexpr int num_bits = 16*8 + 8*3;
	uint32_t lfsr = 0xabcd0123;

	puts("LED matrix driver: Checking serial data path ...");

	puts("sent    :");
	// shift in test pattern into the shift register.
	for(int i = 0; i < num_bits; ++i)
	{
		// unfortunately (fortunately?) there is no need to
		// insert wait between these bit-banging, since
		// ESP32/ESP8266's GPIO is very slow.
		uint8_t bit = lfsr & 1;
		led_print_0_1(bit);
		digitalWrite(IO_COLSER, bit);
		digitalWrite(IO_COLCLK, 1);
		digitalWrite(IO_COLCLK, 0);
		lfsr = led_lfsr(lfsr);
	}
	puts("");

	lfsr = 0xabcd0123;
	puts("received:");
	bool error = false;
	for(int i = 0; i < num_bits; ++i)
	{
		// sense the input pin;
		// we may need some delay here because
		// return path driving the input pin is very weak and noisy path.
		// in practice it seems that there is no need to insert wait here ...
		delayMicroseconds(10);
		int r = digitalRead(IO_HC595SEROUT);
		led_print_0_1(r);
		if(r != (lfsr & 1))
		{
			// error found
			error = true;
		}
		
		digitalWrite(IO_COLCLK, 1);
		digitalWrite(IO_COLCLK, 0);
		lfsr = led_lfsr(lfsr);
	}
	puts("");

	if(error)
	{
		puts("Error found.");
	}
	else
	{
		puts("No error found.");
	}

}



#define NUM_LED1642  8 // number of LED1642 in serial
/**
 * set LED1642 register using bitbanging
 */
static void led_post_set_led1642_reg(int reg, uint16_t val)
{
	for(int i = 0; i < NUM_LED1642; ++i)
	{
		for(int bit = 15; bit >= 0; --bit)
		{
			// set bit
			digitalWrite(IO_COLSER, !!(val & (1<<bit)));
/*
		printf("%2d:%2d:%d:%d:%d:%d:%d:%d:%d:%d\r\n",
			i, bit, !!(val & (1<<bit)), digitalRead(IO_HC595SEROUT),
			digitalRead(25),
			digitalRead(26),
			digitalRead(27),
			digitalRead(14),
			digitalRead(12)
			);
*/
			// latch on
			if(i == (NUM_LED1642 - 1) &&
				bit == reg-1)
			{
				// latch (clock count while latch is active; if reg = 7, set CR function)
				digitalWrite(IO_COLLATCH, 1);
			}
			// clock
			digitalWrite(IO_COLCLK, 1);
			digitalWrite(IO_COLCLK, 0);
		}
		// latch off
		digitalWrite(IO_COLLATCH, 0);
	}
}


typedef uint8_t buf_t;
static buf_t *buf;
#define BUFSZ 4096

static uint16_t led_config; // LED1642's config word
static int current_gain_index = LED_CURRENT_GAIN_MAX; // current gain index 0 .. LED_CURRENT_GAIN_MAX


static void IRAM_ATTR i2s_int_hdl(void *arg);

static volatile lldesc_t *dmaDesc;
#define MAX_DMA_ITEM_COUNT 1024



static void init_dma() {
	// enable the peripheral
	periph_module_enable(PERIPH_I2S1_MODULE);

	// allocate memories
	// note that MALOC_CAP_DMA ensures the memories are reachable from DMA hardware
	buf = (buf_t*)heap_caps_malloc(BUFSZ * sizeof(*buf), MALLOC_CAP_DMA);
	dmaDesc = (lldesc_t*)heap_caps_malloc((BUFSZ / MAX_DMA_ITEM_COUNT) * sizeof(lldesc_t), MALLOC_CAP_DMA);
	memset((void*)buf, 0, BUFSZ * sizeof(*buf));
	memset((void*)dmaDesc, 0, (BUFSZ / MAX_DMA_ITEM_COUNT) * sizeof(lldesc_t));

	//Init pins to i2s functions
	pinMatrixOutAttach(IO_COLCLK, I2S1O_WS_OUT_IDX, true, false); // invert

	pinMatrixOutAttach(IO_COLSER, I2S1O_DATA_OUT0_IDX, false, false); // bit 0
	pinMatrixOutAttach(IO_COLLATCH, I2S1O_DATA_OUT1_IDX, false, false); // bit 1
	pinMatrixOutAttach(IO_ROWLATCH,  I2S1O_DATA_OUT2_IDX, false, false); // bit 2

	//Reset I2S subsystem
	I2S1.conf.rx_reset=1; I2S1.conf.tx_reset=1;
	I2S1.conf.rx_reset=0; I2S1.conf.tx_reset=0;

	I2S1.conf2.val=0;
	I2S1.conf2.lcd_en=1;

	// Both I2S_LCD_TX_SDX2_EN bit and
	// I2S_LCD_TX_WRX2_EN bit are set to 1 in the data frame, form 2
	// ???? TRM mistake ?
	I2S1.conf2.lcd_tx_wrx2_en = 1;
	I2S1.conf2.lcd_tx_sdx2_en = 0;


	I2S1.sample_rate_conf.rx_bits_mod=8;
	I2S1.sample_rate_conf.tx_bits_mod=8;
	I2S1.sample_rate_conf.rx_bck_div_num=2; // min 2
	I2S1.sample_rate_conf.tx_bck_div_num=2; // min 2

	I2S1.clkm_conf.val=0;
	I2S1.clkm_conf.clka_en=0;
	I2S1.clkm_conf.clk_en=0;

	// f(i2s) = fpll / (clkm_div_num + clkm_div_b / clkm_div_a)
	// fpll = defaults to PLL_D2_CLK, 160MHz
	// eg. clkm_div_num = 11, clkm_div_a = 1, clkm_div_b = 1,
	//    tx_bck_div_num = 2 : 
	// the clock rate = 160MHz / (11+1/1) / 2 = 6.666... MHz
	I2S1.clkm_conf.clkm_div_a=1;
	I2S1.clkm_conf.clkm_div_b=1;
	I2S1.clkm_conf.clkm_div_num=7; // min 2

	I2S1.fifo_conf.val=0;
	I2S1.fifo_conf.rx_fifo_mod_force_en=1;
	I2S1.fifo_conf.tx_fifo_mod_force_en=1;
	I2S1.fifo_conf.rx_fifo_mod=1;
	I2S1.fifo_conf.tx_fifo_mod=1;
	I2S1.fifo_conf.rx_data_num=32;
	I2S1.fifo_conf.tx_data_num=32;

	I2S1.conf1.val=0;
	I2S1.conf1.tx_stop_en=1;
	I2S1.conf1.tx_pcm_bypass=1;

	I2S1.conf_chan.val=0;
	I2S1.conf_chan.tx_chan_mod=1;
	I2S1.conf_chan.rx_chan_mod=1;

	I2S1.conf.tx_right_first=1;
	I2S1.conf.rx_right_first=1;
	
	I2S1.timing.val=0;

	// setup interrupts
	esp_intr_alloc(ETS_I2S1_INTR_SOURCE + 0 /*I2S1*/, ESP_INTR_FLAG_IRAM, i2s_int_hdl, (void*)nullptr, nullptr);


	//Reset I2S FIFO
	I2S1.conf.tx_reset=1;
	I2S1.conf.tx_fifo_reset=1;
	I2S1.conf.rx_fifo_reset=1; // I don't know again, rx fifo must also be reset
	I2S1.conf.tx_reset=0;
	I2S1.conf.tx_fifo_reset=0;
	I2S1.conf.rx_fifo_reset=0; 

	//Reset DMA
	I2S1.lc_conf.in_rst=1; // I don't know why but 'in link' must be reset as also as 'out link'
	I2S1.lc_conf.out_rst=1;
	I2S1.lc_conf.in_rst=0;
	I2S1.lc_conf.out_rst=0;

	//Fill DMA descriptor, each MAX_DMA_ITEM_COUNT entries
	volatile lldesc_t * pdma = dmaDesc;
	uint8_t *b = buf;
	int remain = BUFSZ;
	while(remain > 0)
	{
		int one_len = MAX_DMA_ITEM_COUNT < remain ? MAX_DMA_ITEM_COUNT: remain;
		pdma->length=one_len;
		pdma->size=one_len;
		pdma->owner=1;
		pdma->sosf=0;
		pdma->buf=(uint8_t *)b;
		pdma->offset=0; //unused in hw
		pdma->empty= (int32_t)(pdma + 1);
		pdma->eof=0;

		remain -= one_len;
		++pdma;
		b += one_len;
	}

	pdma[-1].empty = (int32_t)(&dmaDesc[0]); // make loop
	dmaDesc[1].eof = 1;
	dmaDesc[3].eof = 1; // make sure these blocks generates the interrupt

	//Set desc addr
	I2S1.out_link.addr=((uint32_t)(&(dmaDesc[0])))&I2S_OUTLINK_ADDR;


	//Enable and configure DMA
	I2S1.lc_conf.val= 	typeof(I2S1.lc_conf)  { {
            .in_rst =             0,
            .out_rst =            0,
            .ahbm_fifo_rst =      0,
            .ahbm_rst =           0,
            .out_loop_test =      0,
            .in_loop_test =       0,
            .out_auto_wrback =    1,
            .out_no_restart_clr = 0,
            .out_eof_mode =       1,
            .outdscr_burst_en =   1,
            .indscr_burst_en =    0,
            .out_data_burst_en =  1,
            .check_owner =        0,
            .mem_trans_en =       0,
	} }.val;


	//Clear int flags
	I2S1.int_clr.val=0xFFFFFFFF;


	I2S1.fifo_conf.dscr_en=1;

	//Start transmission
	I2S1.out_link.start=1;

	// make sure that DMA reads required descriptors and push its first data to FIFO
	while(I2S1.out_link_dscr == 0 &&
		I2S1.out_link_dscr_bf0  == 0 &&
		I2S1.out_link_dscr_bf1 == 0) /**/;

	I2S1.conf.tx_start=1;

	I2S1.int_ena.out_eof = 1; // enable outband eof interrupt


}


/**
 * Gamma curve function
 */
static constexpr uint16_t gamma_255_to_4095(int in)
{
	using std::pow;
  return /*byte_reverse*/(
  	/*bit_interleave*/(
  	(uint16_t) (pow(
		  	(float)((in+5.0f) / (255.0f+5.0f)),
			(float)2.2)
				* 3800)));  
}

#define G4(N) gamma_255_to_4095((N)), gamma_255_to_4095((N)+1), \
      gamma_255_to_4095((N)+2), gamma_255_to_4095((N)+3), 

#define G16(N) G4(N) G4((N)+4) G4((N)+8) G4((N)+12) 
#define G64(N) G16(N) G16((N)+16) G16((N)+32) G16((N)+48) 

/**
 * Gamma curve table
 */
static uint16_t DRAM_ATTR gamma_table[256] = {
	G64(0) G64(64) G64(128) G64(192)
	}; // this table must be accessible from interrupt routine;
	// do not place in FLASH !!



#define B_COLSER (1<<0)
#define B_COLLATCH (1<<1)
#define B_ROWLATCH (1<<2)
#define B_STATUSLED (1<<3)

/*
	To simplify things,
	the LED1642's PWM clock receives exactly the same signal as serial data clock.
	This means the data clock must be synchronized to PWM clock time frame:
	4096 clocks or its integral multiples.
	To reduce PWM clock interfering with WiFi, PWM clock frequency should be as low
	as possible; Here we use the most basic 4096 clocks per one line.

time frame:

16*8 = 128 clocks     :    Pixel brightness data(0) for next line 
16*8 = 128 clocks     :    Pixel brightness data(1) for next line 
16*8 = 128 clocks     :    Pixel brightness data(2) for next line 
              : 
16*8 = 128 clocks     :    Pixel brightness data(11) for next line 

512 clocks            :    dummy

------------------    2048 clock boundary

16*8 = 128 clocks     :    Pixel brightness data(12) for next line
              :
16*8 = 128 clocks     :    Pixel brightness data(14) for next line 

16*8 = 128 clocks     :    config word

1104 clocks           :    dummy

 -- 2048 + 1616 clock boundary

24  clocks            :    All LEDs off for HC(T)595
16*8 = 128 clocks     :    All LEDs off
24  clocks            :    Next row data for HC(T)595
16*8 = 128 clocks     :    HC(T)595 latch + 
                           Pixel brightness data(15) + global latch for next line 
16*8 = 128 clocks     :    All LED on


	The last 256 clocks in a time frame are dead clocks; all leds are off at this period.

*/

static int IRAM_ATTR build_brightness(buf_t *buf, int row, int n)
{
	// build framebuffer content
	frame_buffer_t::array_t & array = get_current_frame_buffer().array();
	for(int i = 0; i < NUM_LED1642; ++i)
	{
		int x = i * 8 + (n >> 1);
		int y = (row << 1) + (n & 1);

		uint16_t br = /*(x%48)==y ? 0xaaa: 0;// */gamma_table[array[y][x]];

		buf[ 0] = (br & (1<<15)) ? B_COLSER : 0;
		buf[ 1] = (br & (1<<14)) ? B_COLSER : 0;
		buf[ 2] = (br & (1<<13)) ? B_COLSER : 0;
		buf[ 3] = (br & (1<<12)) ? B_COLSER : 0;
		buf[ 4] = (br & (1<<11)) ? B_COLSER : 0;
		buf[ 5] = (br & (1<<10)) ? B_COLSER : 0;
		buf[ 6] = (br & (1<< 9)) ? B_COLSER : 0;
		buf[ 7] = (br & (1<< 8)) ? B_COLSER : 0;
		buf[ 8] = (br & (1<< 7)) ? B_COLSER : 0;
		buf[ 9] = (br & (1<< 6)) ? B_COLSER : 0;

		if(i == NUM_LED1642 - 1)
		{
			// do latch
			if(n == 15) // issue global latch at last transfer
			{
				buf[10] = (br & (1<< 5)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
				buf[11] = (br & (1<< 4)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			}
			else
			{
				buf[10] = (br & (1<< 5)) ? B_COLSER : 0;
				buf[11] = (br & (1<< 4)) ? B_COLSER : 0;
			}

			buf[12] = (br & (1<< 3)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[13] = (br & (1<< 2)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[14] = (br & (1<< 1)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[15] = (br & (1<< 0)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
		}
		else
		{
			buf[10] = (br & (1<< 5)) ? B_COLSER : 0;
			buf[11] = (br & (1<< 4)) ? B_COLSER : 0;
			buf[12] = (br & (1<< 3)) ? B_COLSER : 0;
			buf[13] = (br & (1<< 2)) ? B_COLSER : 0;
			buf[14] = (br & (1<< 1)) ? B_COLSER : 0;
			buf[15] = (br & (1<< 0)) ? B_COLSER : 0;
		}
		buf += 16;
	}

	return NUM_LED1642 * 16;
} 


// build resister for resister no. 2
static int IRAM_ATTR build_set_led1642_reg_2(buf_t *buf, uint16_t val)
{
//	return build_set_led1642_reg(buf, 2, val);

	for(int i = 0; i < NUM_LED1642; ++i)
	{
		buf[ 0] = (val & (1<<15)) ? B_COLSER : 0;
		buf[ 1] = (val & (1<<14)) ? B_COLSER : 0;
		buf[ 2] = (val & (1<<13)) ? B_COLSER : 0;
		buf[ 3] = (val & (1<<12)) ? B_COLSER : 0;
		buf[ 4] = (val & (1<<11)) ? B_COLSER : 0;
		buf[ 5] = (val & (1<<10)) ? B_COLSER : 0;
		buf[ 6] = (val & (1<< 9)) ? B_COLSER : 0;
		buf[ 7] = (val & (1<< 8)) ? B_COLSER : 0;
		buf[ 8] = (val & (1<< 7)) ? B_COLSER : 0;
		buf[ 9] = (val & (1<< 6)) ? B_COLSER : 0;
		buf[10] = (val & (1<< 5)) ? B_COLSER : 0;
		buf[11] = (val & (1<< 4)) ? B_COLSER : 0;
		buf[12] = (val & (1<< 3)) ? B_COLSER : 0;
		buf[13] = (val & (1<< 2)) ? B_COLSER : 0;
		if(i == (NUM_LED1642 -1))
		{
			buf[14] = (val & (1<< 1)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[15] = (val & (1<< 0)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
		}
		else
		{
			buf[14] = (val & (1<< 1)) ? B_COLSER : 0;
			buf[15] = (val & (1<< 0)) ? B_COLSER : 0;
		}
		buf += 16;
	}

	return NUM_LED1642 * 16;

}

// build resister for resister no. 7
static int IRAM_ATTR build_set_led1642_reg_7(buf_t *buf, uint16_t val)
{
//	return build_set_led1642_reg(buf, 2, val);

	for(int i = 0; i < NUM_LED1642; ++i)
	{
		buf[ 0] = (val & (1<<15)) ? B_COLSER : 0;
		buf[ 1] = (val & (1<<14)) ? B_COLSER : 0;
		buf[ 2] = (val & (1<<13)) ? B_COLSER : 0;
		buf[ 3] = (val & (1<<12)) ? B_COLSER : 0;
		buf[ 4] = (val & (1<<11)) ? B_COLSER : 0;
		buf[ 5] = (val & (1<<10)) ? B_COLSER : 0;
		buf[ 6] = (val & (1<< 9)) ? B_COLSER : 0;
		buf[ 7] = (val & (1<< 8)) ? B_COLSER : 0;
		buf[ 8] = (val & (1<< 7)) ? B_COLSER : 0;
		if(i == (NUM_LED1642 -1))
		{
			buf[ 9] = (val & (1<< 6)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[10] = (val & (1<< 5)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[11] = (val & (1<< 4)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[12] = (val & (1<< 3)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[13] = (val & (1<< 2)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[14] = (val & (1<< 1)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
			buf[15] = (val & (1<< 0)) ? (B_COLSER|B_COLLATCH) : B_COLLATCH;
		}
		else
		{
			buf[ 9] = (val & (1<< 6)) ? B_COLSER : 0;
			buf[10] = (val & (1<< 5)) ? B_COLSER : 0;
			buf[11] = (val & (1<< 4)) ? B_COLSER : 0;
			buf[12] = (val & (1<< 3)) ? B_COLSER : 0;
			buf[13] = (val & (1<< 2)) ? B_COLSER : 0;
			buf[14] = (val & (1<< 1)) ? B_COLSER : 0;
			buf[15] = (val & (1<< 0)) ? B_COLSER : 0;
		}
		buf += 16;
	}

	return NUM_LED1642 * 16;

}


static void IRAM_ATTR shuffle_bytes(buf_t *buf, int count)
{
	uint32_t *p32 = (uint32_t *)buf;
	for(int i = 0; i < count / sizeof(uint32_t) * sizeof(buf_t); ++i)
	{
		p32[i] = (p32[i] >> 16) | (p32[i] << 16);
	}
}


static volatile int r = 0; // current row

#define HALF_BUILD_BOUNDARY 11

void IRAM_ATTR build_first_half()
{
	buf_t *bufp = buf;

	for(int n = 0; n <= HALF_BUILD_BOUNDARY; ++n)
	{
		bufp += build_brightness(bufp, r, n);
	}

	while(bufp < buf + 2048)
	{
		// dummy clock
		*(bufp++)  = 0;
	}

	// word order shuffle
	shuffle_bytes(buf, 2048);
}



void IRAM_ATTR build_second_half()
{

	buf_t *bufp = buf + 2048;
	buf_t *tmpp;

	for(int n = HALF_BUILD_BOUNDARY+1; n <= 14; ++n)
	{
		bufp += build_brightness(bufp, r, n);
	}

	bufp += build_set_led1642_reg_7(bufp, led_config); // build LED1642 config word

	while(bufp < buf + (2048 + 1616))
	{
		// dummy clock
		*(bufp++)  = 0;
	}

	// all LED off
	for(int i = 0; i < 24; ++ i)
	{
		*(bufp++) = B_COLSER;
	}

	bufp += build_set_led1642_reg_2(bufp, 0x0000); // full LEDs off

	// row select
	tmpp = bufp; // remember current position 
	for(int i = 0; i < 24; ++ i)
	{
		buf_t t = 0;
		if(i != r) t |= B_COLSER;
		*(bufp++) = t;
	}

	tmpp[0] |= B_ROWLATCH; // let HCT595 latch the buffer with '1' (clear all LED)
	tmpp[1] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long
	tmpp[2] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long
	tmpp[3] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long

	bufp += build_brightness(bufp, r, 15); // global latch of brightness data

	tmpp = bufp; // remember current position

	bufp += build_set_led1642_reg_2(bufp, 0xffff); // full LEDs on

	tmpp[0] |= B_ROWLATCH; // let HCT595 latch the buffer
	tmpp[1] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long
	tmpp[2] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long
	tmpp[3] |= B_ROWLATCH; // To make sure, let latch pulse reasonably long

	// word order shuffle
	shuffle_bytes(buf + 2048, 2048);


}


uint8_t matrix_button_scan_bits; //!< holds currently pushed button bit-map ('1':pushed)
static void IRAM_ATTR scan_button()
{
	int btn_num = r-2; // 'r' represents currently buffering row + 1, so subtract 2 from it
	if(btn_num >= 0 && btn_num < MAX_BUTTONS)
	{
		typeof(matrix_button_scan_bits) mask = 1 << btn_num;
		typeof(matrix_button_scan_bits) tmp = matrix_button_scan_bits;
		tmp &= ~mask;
		if(!digitalRead(IO_BUTTONSENSE))
			tmp |= mask;
		matrix_button_scan_bits = tmp;
	}
}


// interrupt routine invoked by DMA eof signal
static void IRAM_ATTR matrix_drive_fill_buffer()
{
	if(dmaDesc[1].owner == 0)
	{
		dmaDesc[1].owner = 1;
		build_first_half();
		scan_button();
	}
	if(dmaDesc[3].owner == 0)
	{
		dmaDesc[3].owner = 1;
		build_second_half();
		++r;
		if(r >= 24) r = 0;
	}
}



static volatile int intr_count = 0;

// i2s interrupt handler
static void IRAM_ATTR i2s_int_hdl(void *arg) {
	++intr_count;
	if (I2S1.int_st.out_eof) {
		I2S1.int_clr.val = I2S1.int_st.val;
		matrix_drive_fill_buffer();
	}
}



void matrix_drive_loop() {
	/* nothing */
}


//static void refresh_task(void* arg);

void matrix_drive_early_setup()
{
	// blank all matrix LEDs
	led_gpio_init();

	led_gpio_set_low();

	led_hard_reset_led1642();

	delay(1000);

	// Here we should repeat setting configuration register several times
	// at least twice of number of LED1642.
	// We should enable SDO delay because the timing is too tight to
	// transfer data on the serial chain if no SDO delay is applied.
	// Because SDO delay would be set properly only if the previous
	// LED1642 on the chain has delay on the data,
	// so we must set SDO delay one by one from the first LED1642 on the chain
	// to make all LED1642s have proper configuration.
	led_config = 0;
	led_config += (1<<13) | (1<<14);  // enable SDO delay and progressive delay
	led_config += (1<<11) | (1<<12) |(1<<15); // Output turn-on/off time: on:180ns, off:150ns
	led_config += 0b111111 | (1<<6); // set current gain
	for(int i = 0; i <NUM_LED1642 * 4; ++i)
	{
		led_post_set_led1642_reg(7, led_config); // set control register
	}

	led_post_set_led1642_reg(2, 0); // all blank
}

void matrix_drive_setup() {
	puts("Matrix LED driver initializing ...");

	frame_buffer_t::array_t & array = get_current_frame_buffer().array();
	for(int y = 0; y < 48; ++y)
		for(int x = 0; x < 64; ++x)
			array[y][x] = 255;



	led_post();

	led_hard_reset_led1642(); // again, reset and set control register for all LED1642's
	int clock = NUM_LED1642 * 4 * 128;
	for(int i = 0; i <NUM_LED1642 * 4; ++i)
	{
		led_post_set_led1642_reg(7, led_config); // set control register
	}

	led_post_set_led1642_reg(1, 0xffff); // full LEDs on
	clock += 128;

	// before doing DMA operation,
	// send dummy clocks to LED1642, to set internal PWM counter as ZERO
	// note that PWM counter overflows at 4096 and becomes zero.
	clock = 4096 - clock % 4096;
	for(int i = 0; i < clock; ++i)
	{
		digitalWrite(IO_COLCLK, 1);
		digitalWrite(IO_COLCLK, 0);
	}

	// at this point, LED1642's internal PWM counter must be zero

	init_dma();

//	xTaskCreatePinnedToCore(refresh_task, "LED_Refresh", 4096, NULL, 1, NULL, 0);


}


// set led current gain.
// valid range is: 0 to LED_CURRENT_GAIN_MAX 
void matrix_drive_set_current_gain(int gain)
{
	// LED1642 has two range of current settings: high and low.
	// ah ... so, below certain current value, we will use low
	// current range, otherwise high current range.
	bool range = false; // false:low true:high
	if(gain < 0) gain = 0;
	if(gain > LED_CURRENT_GAIN_MAX) gain = LED_CURRENT_GAIN_MAX;
	current_gain_index = gain;

	if(gain >= 40)
	{
		range = true; // use high range
		gain -= 40;
		gain += 0;
	}

	uint16_t config = led_config;
	config &= ~(1<<6); // clear range bit
	if(range) config |= (1<<6); // use high range
	config &= ~ 0b111111; // the target bits are located at LSB
	config |= (uint16_t) gain;
	led_config = config;
}

// get current gain of LED1642
int matrix_drive_get_current_gain()
{
	return current_gain_index;
}
#if 0

#define W 160
#define H 60

static unsigned char buffer[H][W] = { {0} };

static int sv[W] = {0};
static int ssv[W] = {0};

static void step()
{
	for(int x = 0; x < W; x++)
	{
		sv[x] += ssv[x];
		if(sv[x] < 0) sv[x] = 0, ssv[x] +=4;
		if(sv[x] > 255) sv[x] = 255, ssv[x] -=4;
		ssv[x] += rand() %10-5;
		buffer[H-1][x] = sv[x];
	}


	for(int y = 0; y < H-1; y++)
	{
		for(int x = 1; x < W-1; x++)
		{
			if(y < H-3 && buffer[y+3][x] > 50)
			{
				buffer[y][x] = 
					(
					buffer[y+2][x-1] + 
					buffer[y+3][x  ]*6 + 
					buffer[y+2][x+1] +

					buffer[y+1][x-1] + 
					buffer[y+2][x  ]*6 + 
					buffer[y+1][x+1] 
					
					)  / 16; 
			}
			else
			{
				buffer[y][x] = 
					(
					buffer[y+1][x-1] + 
					buffer[y+1][x  ]*2 + 
					buffer[y+1][x+1] )  / 4; 
			}
		}
	}
}



static void refresh_task(void* arg) {
  while (1) {
	delay(20);
	  step();
  	frame_buffer_t::array_t & array = get_current_frame_buffer().array();

	for(int y = 0; y < 48; y++)
	{
		memcpy(array[y], buffer[y] + 10, 64);
	}

  }
}







#endif