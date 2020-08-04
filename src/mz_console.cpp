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

static const char * const history_file_name = "/settings/.history";
static bool dumb_mode = true;

#define DUMB_PROMPT  "> "
#define ANSI_PROMPT  "\x1b[32m" "> " "\x1b[39m"

void console_probe()
{
	if (linenoiseProbe()) { /* zero indicates success */
          printf("\n"
	    "Your terminal application does not support escape sequences.\n"
	    "Line editing and history features are disabled.\n"
	    "On linux , try screen.\n"
	    "On Windows, try using Putty instead.\n");
          linenoiseSetDumbMode(1);
		dumb_mode = true;
  	}
	else
	{
        printf("Line editing and history features enabled.\n");
		linenoiseSetDumbMode(0);
		dumb_mode = false;
	}
}


/**
 * Console task handler
 * */
static void console_task(void *)
{
	// Note, this function is to be run inside separete thread
	char *line;
	for(;;) {
		if(dumb_mode)
			printf("\n"
			"info: Command line editing and history features are currently disabled.\n"
			"info: Try 't<enter>' to enable command line editing and history.\n");

		if((line = linenoise(dumb_mode ? DUMB_PROMPT : ANSI_PROMPT)) != NULL) {
			linenoiseHistoryAdd(line);

			linenoiseHistorySave(history_file_name);

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

	console_config.max_cmdline_args = 16;
	console_config.max_cmdline_length = 256;
	console_config.hint_color = 36; // cyan ??? doesn't work well at this IDF version
	console_config.hint_bold = 1;

	ESP_ERROR_CHECK( esp_console_init(&console_config) );

	linenoiseHistorySetMaxLen(30);

	initialize_commands();
}


static void load_console_history()
{
	linenoiseHistoryLoad(history_file_name);
}

void begin_console()
{
	load_console_history();

	console_probe();

	xTaskCreate(
	      console_task,           /* Task function. */
	      "console Task",        /* name of task. */
	      8192,                    /* Stack size of task */
	      (void *)0,                     /* parameter of the task */
	      1,                        /* priority of the task */
	      nullptr); /* Task handle to keep track of created task */
}

