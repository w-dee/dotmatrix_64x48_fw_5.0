#define Binary_h // a kind of nasty thing: disable warning about redefinition of BXXXX
#include <Arduino.h>
#include <Console.h>

static Console CON;

void init_console()
{
	CON.begin(115200, "HAL> ", 10);

	if (CON.termProbe()) { /* zero indicates success */
          printf("\n"
	    "Your terminal application does not support escape sequences.\n"
	    "Line editing and history features are disabled.\n"
	    "On linux , try screen.\n"
	    "On Windows, try using Putty instead.\n");
          CON.termDumb(true);
  	}

	CON.consoleTaskStart( );  // will start a task waiting for input but only print back

}