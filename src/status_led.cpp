#include "soc/rtc.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "status_led.h"
#include "rom/lldesc.h"
#include "soc/periph_defs.h"
#include "soc/uhci_reg.h"
#include "soc/uhci_struct.h"
#include "driver/periph_ctrl.h"
#include "interval.h"
#include <algorithm>

// we use here inverted UART to transmit WS2812 signals.
// using 3.333...MHz baud rate and 6bit transmission mode and
// output inversion enabled,
//  st  0  1  2  3  4  5  sp
//   1  x  x  x  x  x  x  0 
// above waveform can be transmitted. 
// we can use two WS2812 bit in one transmittion unit:
// upper nibble and lower nibble.
// each nibble can be:
// for WS2812's 0 symbol:   0b 1100
// for WS2812's 1 symbol:   0b 1110

// The important thing is: the data sent must not be interrupted
// (must be continuous)
// until the "reset" signal (long LOW level on line).
// So we first build the data which are ready to be sent,
// then use DMA.


#define IO_STATUSLED 4

s_rgb_t status_led_array[MAX_STATUS_LED];
static lldesc_t *dma_desc = nullptr;
static uint8_t *status_led_buf = nullptr;
#define status_led_buf_sz (MAX_STATUS_LED * 24 / 2)
static_assert(status_led_buf_sz % 4 == 0, "status_led_buf_sz must be multiple of 4");
	// I'm not sure that DMA engine can do byte-wise transmit

static int brightness = 256; // brightness

static constexpr long unsigned int  ONEBIT_NS = (1200UL); // time needed to send one bit (in ns)
static constexpr long unsigned int ONELED_NS = (ONEBIT_NS*24); // time needed to send one led (in ns)
static constexpr long unsigned int TOTAL_LEDS_NS = (ONELED_NS * MAX_STATUS_LED); // total time needed to send all leds (in ns)
static constexpr long unsigned int  TOTAL_TIME_NS =  // total time needed to send all leds + RESET signal + some headroom (in ns)
	(TOTAL_LEDS_NS + 600000UL); 

static constexpr uint32_t TOTAL_TIME_MS = (TOTAL_TIME_NS - 1) / 1000000 + 1 + 1; // total time in ms + headroom


// ??? std:max is not constexpr dakke?
static constexpr uint32_t sl_max(uint32_t a, uint32_t b) { return a>b ? a: b; }

static constexpr uint32_t UPDATE_INTERVAL = sl_max(TOTAL_TIME_MS, static_cast<typeof(TOTAL_TIME_MS)>(20));


static void init_dma_desc()
{
	// allocate buffers
	if(!status_led_buf)
	{
		status_led_buf = (uint8_t*)heap_caps_malloc(status_led_buf_sz, MALLOC_CAP_DMA);
		memset((void*)status_led_buf, 0, status_led_buf_sz);
	}
	if(!dma_desc)
	{
		dma_desc = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);
		memset((void*)dma_desc, 0, sizeof(lldesc_t));
	}
	if(!status_led_buf || !dma_desc) return; // TODO: error check

	// enable UHCI1 module
	// UHCI -- Universal Host Controller Interface. I don't know why
	// they call this DMA block as UHCI, irrelevant of USB.
	periph_module_enable(PERIPH_UHCI1_MODULE);

	// fill DMA desc
	dma_desc->length=status_led_buf_sz;
	dma_desc->size=status_led_buf_sz;
	dma_desc->owner=1;
	dma_desc->sosf=0;
	dma_desc->buf=(uint8_t *)status_led_buf;
	dma_desc->offset=0; //unused in hw
	dma_desc->empty=0;
	dma_desc->eof=1;

	// DMA reset
	UHCI1.conf0.ahbm_fifo_rst = 1;
	UHCI1.conf0.ahbm_rst = 1;
	UHCI1.conf0.in_rst = 1;
	UHCI1.conf0.out_rst = 1;
	UHCI1.conf0.ahbm_fifo_rst = 0;
	UHCI1.conf0.ahbm_rst = 0;
	UHCI1.conf0.in_rst = 0;
	UHCI1.conf0.out_rst = 0;
	
	// DMA configure
	UHCI1.conf0.val = 0;
	UHCI1.conf0.uart2_ce = 1;
	UHCI1.conf1.val = 0;
	UHCI1.int_clr.val = 0xffffffff;

}

// prepare transmit data which is ready-to-send via DMA
static void _commit_status_led()
{

#define SYMBOL_H_LOWER_NIBBLE (0b110<<0)
#define SYMBOL_L_LOWER_NIBBLE (0b111<<0)
#define SYMBOL_H_UPPER_NIBBLE (0b100<<3)
#define SYMBOL_L_UPPER_NIBBLE (0b110<<3)
	
	// prepare tx buffer
	uint8_t *buf = status_led_buf;
	for(int i = 0; i < MAX_STATUS_LED; ++i)
	{
		s_rgb_t val = status_led_array[i];
		val.b = val.b * brightness >> 8;
		val.g = val.g * brightness >> 8;
		val.r = val.r * brightness >> 8;
		uint32_t v = val.value;
		buf[0] =
			( v & (1<<23) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<22) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[1] =
			( v & (1<<21) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<20) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[2] =
			( v & (1<<19) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<18) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[3] =
			( v & (1<<17) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<16) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[4] =
			( v & (1<<15) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<14) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[5] =
			( v & (1<<13) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<12) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[6] =
			( v & (1<<11) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<<10) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[7] =
			( v & (1<< 9) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<< 8) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[8] =
			( v & (1<< 7) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<< 6) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[9] =
			( v & (1<< 5) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<< 4) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[10] =
			( v & (1<< 3) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<< 2) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf[11] =
			( v & (1<< 1) ? SYMBOL_H_LOWER_NIBBLE : SYMBOL_L_LOWER_NIBBLE ) |
			( v & (1<< 0) ? SYMBOL_H_UPPER_NIBBLE : SYMBOL_L_UPPER_NIBBLE ) ;
		buf += 12;
	}

}



// drive WS2812 using UART with DMA
void status_led_commit()
{
	// prepare for sending the data
	_commit_status_led();

	// wait previous transmit
	// TODO: precious timing estimation
	static unsigned long last_transmit_millis;
	signed long wait_time = (last_transmit_millis + TOTAL_TIME_MS) - millis();
	if(wait_time > 0 && wait_time <= TOTAL_TIME_MS)
		delayMicroseconds(wait_time * 1000);

	// send new data
	UHCI1.dma_out_link.addr = (uint32_t)dma_desc;
	UHCI1.int_clr.val = 0xffffffff;
	UHCI1.dma_out_link.start = 1;
	last_transmit_millis = millis();
}


// early initialization code to blank all LEDs
void status_led_early_setup()
{
	init_dma_desc();
	pinMode(IO_STATUSLED, OUTPUT);
    pinMatrixOutDetach(IO_STATUSLED, false, false);
    digitalWrite(IO_STATUSLED, LOW);

	// setup uart config
	uart_config_t uart_config = {
			.baud_rate = 3333333,
			.data_bits = UART_DATA_6_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
			.rx_flow_ctrl_thresh = 120,
			.use_ref_tick = 0,
	};

	uart_param_config(UART_NUM_2, &uart_config);

	uart_set_pin(UART_NUM_2, IO_STATUSLED, UART_PIN_NO_CHANGE,
		UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_TXD_INV);

	// blank all leds
	delayMicroseconds(500); // wait for WS2812 recognizes the first "reset" signal
	memset(status_led_array, 0, sizeof(status_led_array));
	status_led_commit();
}

void status_led_setup()
{

}

void status_led_loop()
{
	EVERY_MS(UPDATE_INTERVAL)
	{
		status_led_commit();
	}
	END_EVERY_MS
}

/**
 * set status led global brightness
 * */
void status_led_set_global_brightness(int v)
{
	brightness = v;
}