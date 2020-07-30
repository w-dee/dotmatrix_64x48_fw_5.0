#define Binary_h // a kind of nasty thing: disable warning about redefinition of BXXXX
#include <Arduino.h>
#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_fat.h"
#include "threadsync.h"



static void console_task(void *)
{
	// Note, this function is to be run inside separete thread
	char *line;
	for(;;) {

		if((line = linenoise("> ")) != NULL) {
			linenoiseHistoryAdd(line);

			//printf("got: %s\r\n", LastLine.c_str());
			int ret_val = 0;
			esp_err_t err = esp_console_run(line, &ret_val);
			if(err == ESP_ERR_NOT_FOUND)
			{
				printf("Command not found. Type 'help' to show help.\n");
			}

			linenoiseFree(line); 
		}
	}
}


void init_console_commands(); // in console_commands.cpp

static void initialize_commands()
{
	esp_console_register_help_command();
	init_console_commands();
}

void init_console()
{
	uart_config_t uart_config;
	uart_config.baud_rate = 115200;
	uart_config.data_bits = UART_DATA_8_BITS;
	uart_config.parity = UART_PARITY_DISABLE;
	uart_config.stop_bits = UART_STOP_BITS_1;
	uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uart_param_config(UART_NUM_1, &uart_config);

	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
	esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
	/* Move the caret to the beginning of the next line on '\n' */
	esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

	/* Install UART driver for interrupt-driven reads and writes */
	ESP_ERROR_CHECK( uart_driver_install((uart_port_t)CONFIG_CONSOLE_UART_NUM,
						256, 0, 0, NULL, 0) );

	/* Tell VFS to use UART driver */
	esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);

	/* Initialize the console */
	esp_console_config_t console_config;

	console_config.max_cmdline_args = 8;
	console_config.max_cmdline_length = 256;
	#if CONFIG_LOG_COLORS
	console_config.hint_color = atoi(LOG_COLOR_CYAN);
	#endif

	ESP_ERROR_CHECK( esp_console_init(&console_config) );

	linenoiseHistorySetMaxLen(10);

	initialize_commands();
}

void begin_console()
{
	if (linenoiseProbe()) { /* zero indicates success */
          printf("\n"
	    "Your terminal application does not support escape sequences.\n"
	    "Line editing and history features are disabled.\n"
	    "On linux , try screen.\n"
	    "On Windows, try using Putty instead.\n");
          linenoiseSetDumbMode(1);
  	}


	xTaskCreate(
	      console_task,           /* Task function. */
	      "console Task",        /* name of task. */
	      10000,                    /* Stack size of task */
	      (void *)0,                     /* parameter of the task */
	      1,                        /* priority of the task */
	      nullptr); /* Task handle to keep track of created task */
}

