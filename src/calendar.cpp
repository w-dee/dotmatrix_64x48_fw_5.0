#include <Arduino.h>
#include "calendar.h"
#include "settings.h"
#include "lwip/apps/sntp.h"
#include <time.h>
#include <stdlib.h>

static string_vector time_servers;
static String time_zone;

/**
 * reconfigure underlying time keeping facility
 * */
static void reconfigure()
{
    if(time_servers.size() == 0 || time_servers[0].length() == 0 || time_zone.length() == 0)
        return; // error: not configurable
    configTzTime(time_zone.c_str(),
        time_servers.size() >= 1 ? time_servers[0].c_str() : nullptr,
        time_servers.size() >= 2 ? time_servers[1].c_str() : nullptr,
        time_servers.size() >= 3 ? time_servers[2].c_str() : nullptr);
}


/**
 * initialize the calendar
 * */
void init_calendar()
{
    // read from settings
    settings_write_vector(F("time_servers"),
        {"0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"}, SETTINGS_NO_OVERWRITE);
	settings_write(F("time_zone"), F("JST-9"), SETTINGS_NO_OVERWRITE);

	settings_read_vector(F("time_servers"), time_servers);
    if(time_servers.size() > 3) time_servers.resize(3); // only three servers are supported
	settings_read(F("time_zone"), time_zone);

    reconfigure();
}

/**
 * write current settings to settings store
 * */
static void write_settings()
{
	settings_write_vector(F("time_servers"), time_servers);
	settings_write(F("time_zone"), time_zone);
}


/**
 * configure time server and time zone
 * */
void set_tz(const string_vector & _time_servers, const String & _time_zone)
{
    time_servers = _time_servers;
    if(time_servers.size() > 3) time_servers.resize(3); // only three servers are supported
    time_zone = _time_zone;
    reconfigure();
    write_settings();
}

/**
 * get current configured time server and time zone
 * */
void get_tz(string_vector & _time_servers, String & _time_zone)
{
    _time_servers = time_servers;
    _time_zone = time_zone;
}
