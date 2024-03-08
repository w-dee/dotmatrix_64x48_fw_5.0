#include <Arduino.h>
#include <WiFi.h>
#include "matrix_drive.h"
//#include SSID_H
#include "settings.h"
#include "mz_wifi.h"
#include "esp_wps.h"
#include "calendar.h"
#include "ESPmDNS.h"

static bool clear_wifi_setting = false;

// set flag to clear wifi settings (for recovery from the panic)
void wifi_set_clear_setting_flag()
{
	clear_wifi_setting = true;
}


ip_addr_settings_t::ip_addr_settings_t()
{
	clear();
}

void ip_addr_settings_t::clear()
{
	ip_addr =
	ip_gateway =
	ip_mask =
	dns1 =
	dns2 = F("0.0.0.0");
}

void ip_addr_settings_t::dump(const char * address_zero_comment) const
{
	printf("IPv4 address     : %s", ip_addr.c_str());
	if(ip_addr == null_ip_addr && address_zero_comment)
	{
		printf(" %s", address_zero_comment);
	}
	printf("\n");
	printf("IPv4 gateway     : %s\n", ip_gateway.c_str());
	printf("IPv4 subnet mask : %s\n", ip_mask.c_str());
	printf("IPv4 DNS serv 1  : %s\n", dns1.c_str());
	printf("IPv4 DNS serv 2  : %s\n", dns2.c_str());

}

#define MDNS_HOSTNAME "mz5"
#define MDNS_INSTANCE_NAME "MZ5 Dot Matrix Clock"

/**
 * Initialize mDNS responder
 * */
static void init_mDNS()
{
	MDNS.begin(MDNS_HOSTNAME);
	MDNS.setInstanceName(String(MDNS_INSTANCE_NAME));
	MDNS.addService("http", "tcp", 80);
}

/*
	currently only PBC mode (no PIN) is implemented
	taken from https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WPS/WPS.ino
*/
#define ESP_WPS_MODE      WPS_TYPE_PBC
#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"

static esp_wps_config_t config;
static String ap_name;
static String ap_pass;
static ip_addr_settings_t ip_addr_settings;

static void wpsInitConfig(){
//  config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
  config.wps_type = WPS_TYPE_PBC;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

static String wpspin2string(uint8_t a[]){
  char wps_pin[9];
  for(int i=0;i<8;i++){
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}

static WiFiEvent_t wps_last_state = ARDUINO_EVENT_WIFI_READY;
static bool reconfigure_ntp = false;

static void WiFiEventHandler(WiFiEvent_t event, arduino_event_info_t info){
  switch(event){
    case SYSTEM_EVENT_STA_START:
//      puts("Station mode started");
      break;

    case SYSTEM_EVENT_STA_GOT_IP:
//	  printf("Connected to : %s\r\n", String(WiFi.SSID()).c_str() );
//      printf("Got IPv4: %s\r\n", WiFi.localIP().toString().c_str() );
//      printf("Got IPv6: %s\r\n", WiFi.localIPv6().toString().c_str() );
	reconfigure_ntp = true; // to reconfigure NTP client to get current time as fast as possible
      break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
//      puts("Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;

    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
//      printf("WPS successful, stopping WPS and connecting to: %s\r\n", String(WiFi.SSID()).c_str() );

	  wps_last_state = event;
      esp_wifi_wps_disable();

		delay(10);
		// read WPS configuration regardless of WPS success or not
		ap_name = WiFi.SSID();
		ap_pass = WiFi.psk();
		{
			ip_addr_settings_t settings;
			wifi_manual_ip_info(settings); // use DHCP; this also writes current SSID/PSK settings
		}

      WiFi.begin();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
//      puts("WPS failed.");
	  wps_last_state = event;
      esp_wifi_wps_disable();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
//      puts("WPS timed out.");
	  wps_last_state = event;
      esp_wifi_wps_disable();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
      printf("WPS_PIN = %s\r\n", wpspin2string(info.wps_er_pin.pin_code).c_str());
      break;
    default:
      break;
  }
	wifi_check();
}


static void wifi_init_settings();
void wifi_setup()
{
	puts("WiFi initializing ...");

	// check settings fs
	wifi_init_settings();

	// register event handler
	WiFi.onEvent(WiFiEventHandler);

	// first, disconnect wifi
	WiFi.mode(WIFI_OFF);
	WiFi.setAutoReconnect(true);

	// set to Station Mode
	WiFi.mode(WIFI_STA);

	// initialize mDNS
	init_mDNS();
}




static int wifi_last_status = -1;
void wifi_check()
{
	// output to console
	int st = WiFi.status();
	if(wifi_last_status != st)
	{
		wifi_last_status = st;
/*
		puts("");
		String m = wifi_get_connection_info_string();
		printf("%s", m.c_str());
		puts("");
*/
	}
	if(reconfigure_ntp)
	{
		reconfigure_ntp = false;
		calendar_reconfigure();
	}
}


/**
 * Begins WPS configuration.
 * This function *does* return before WPS config ends
 */
void wifi_wps()
{
	wpsInitConfig();
	wps_last_state = ARDUINO_EVENT_WIFI_READY;
	esp_wifi_wps_enable(&config);
	esp_wifi_wps_start(0);
}

/**
 * Returns last WPS status
 * */
WiFiEvent_t wifi_get_wps_status() { return wps_last_state; }

/**
 * Stops WPS configuration.
 * */
void wifi_stop_wps()
{
	esp_wifi_wps_disable();
}


/**
 * Start WiFi connection using configured parameters
 */
void wifi_start()
{
	puts("Starting WiFi ...");

	WiFi.mode(WIFI_OFF);
	WiFi.setAutoReconnect(true);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ap_name.c_str(), ap_pass.c_str());

	IPAddress i_ip_addr;    i_ip_addr   .fromString(ip_addr_settings.ip_addr);
	IPAddress i_ip_gateway; i_ip_gateway.fromString(ip_addr_settings.ip_gateway);
	IPAddress i_ip_mask;    i_ip_mask   .fromString(ip_addr_settings.ip_mask);
	IPAddress i_dns1;       i_dns1      .fromString(ip_addr_settings.dns1);
	IPAddress i_dns2;       i_dns2      .fromString(ip_addr_settings.dns2);

	if(i_ip_addr.toString() != null_ip_addr)
	{
		// if not in DHCP mode ...
		if(i_ip_mask.toString() == null_ip_addr)
		{
			// if the network mask is null
			// this will panic the wifi subsystem of ESP-IDF.
			// set dummy network mask instead.
			i_ip_mask.fromString("255.255.255.0");
		}
	}

	WiFi.config(i_ip_addr, i_ip_gateway, i_ip_mask, i_dns1, i_dns2);
}

/**
 * Read settings. Initialize settings to factory state, if the settings key is invalid
 */
static void wifi_init_settings()
{
	settings_overwrite_t overwrite = SETTINGS_NO_OVERWRITE;
	if(clear_wifi_setting)
	{
		puts("Clearing WiFi settings due to repeated boot failure.");
		overwrite = SETTINGS_OVERWRITE;
	}

	settings_write(F("ap_name"), F(""), overwrite);
	settings_write(F("ap_pass"), F(""), overwrite);
	settings_write(F("ip_addr"), F("0.0.0.0"), overwrite); // automatic ip configuration
	settings_write(F("ip_gateway"), F("0.0.0.0"), overwrite);
	settings_write(F("ip_mask"), F("0.0.0.0"), overwrite);
	settings_write(F("dns_1"), F("0.0.0.0"), overwrite);
	settings_write(F("dns_2"), F("0.0.0.0"), overwrite);

	settings_read(F("ap_name"), ap_name);
	settings_read(F("ap_pass"), ap_pass);
	settings_read(F("ip_addr"),    ip_addr_settings.ip_addr);
	settings_read(F("ip_gateway"), ip_addr_settings.ip_gateway);
	settings_read(F("ip_mask"),    ip_addr_settings.ip_mask);
	settings_read(F("dns_1"),      ip_addr_settings.dns1);
	settings_read(F("dns_2"),      ip_addr_settings.dns2);
}

/**
 * Write settings
 */
void wifi_write_settings()
{
	settings_write(F("ap_name"), ap_name);
	settings_write(F("ap_pass"), ap_pass);
	settings_write(F("ip_addr"),    ip_addr_settings.ip_addr);
	settings_write(F("ip_gateway"), ip_addr_settings.ip_gateway);
	settings_write(F("ip_mask"),    ip_addr_settings.ip_mask);
	settings_write(F("dns_1"),      ip_addr_settings.dns1);
	settings_write(F("dns_2"),      ip_addr_settings.dns2);
}

const String & wifi_get_ap_name()
{
	return ap_name;
}

const String & wifi_get_ap_pass()
{
	return ap_pass;
}

ip_addr_settings_t wifi_get_ip_addr_settings(bool use_current_config)
{
	// if use_current_config is true, returns current connection information
	// instead of manually configured settings
	if(!use_current_config)
		return ip_addr_settings;

	if(WiFi.status() != WL_CONNECTED)
		return ip_addr_settings;

	ip_addr_settings_t s;

	s.ip_addr = WiFi.localIP().toString();
	s.ip_gateway = WiFi.gatewayIP().toString();
	s.ip_mask = WiFi.subnetMask().toString();
	s.dns1 = WiFi.dnsIP(0).toString();
	s.dns2 = WiFi.dnsIP(1).toString();
	return s;
}

void wifi_set_ap_info(const String &_ap_name, const String &_ap_pass)
{
	ap_name = _ap_name;
	ap_pass = _ap_pass;

	wifi_write_settings();
	wifi_start();
}

void wifi_set_ap_info(const String &_ap_name, const String &_ap_pass,
	const ip_addr_settings_t & ip)
{
	ap_name = _ap_name;
	ap_pass = _ap_pass;

	wifi_manual_ip_info(ip);
}

void wifi_manual_ip_info(const ip_addr_settings_t & ip)
{
	ip_addr_settings = ip;

	wifi_write_settings();

	wifi_start();
}

String wifi_get_connection_info_string()
{
	String m;
	switch(WiFi.status())
	{
	case WL_CONNECTED:
		m = String(F("Connected to \"")) + wifi_get_ap_name() + F("\"");
		if((uint32_t)WiFi.localIP() == 0)
			m += String(F(", but no IP address got."));
		else
			m += String(F(", IP address is \"")) + WiFi.localIP().toString() + F("\".");
		break;

	case WL_NO_SSID_AVAIL:
		m = String(F("AP \"")) + wifi_get_ap_name() + F("\" not available.");
		break;

	case WL_CONNECT_FAILED:
		m = String(F("Connection to \"")) + wifi_get_ap_name() + F("\" failed.");
		break;

	case WL_IDLE_STATUS: // ?? WTF ??
		m = String(F("Connection to \"")) + wifi_get_ap_name() + F("\" is idling.");
		break;

	case WL_DISCONNECTED:
		m = String(F("Disconnected from \"")) + wifi_get_ap_name() + F("\".");
		break;

	case WL_CONNECTION_LOST:
		m = String(F("Connection to \"")) + wifi_get_ap_name() + F("\" was lost.");
		break;

	default:
		break;
	}
	return m;
}

bool validate_ipv4_address(const String &string_addr)
{
	IPAddress addr;
	if(!addr.fromString(string_addr)) return false; // IPAddress::fromString validates the string
	return true;	
}

bool validate_ipv4_netmask(const String  &string_addr)
{
	IPAddress addr;
	if(!addr.fromString(string_addr)) return false; // IPAddress::fromString validates the string

	// is valid netmask ??
	// lower index is higher octet;
	uint32_t v = (addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + (addr[3] << 0);

	bool valid = true;
	bool one = true;
	for(int i = 31; i>=0; --i)
	{
		if(!(v & (1<<i)))
		{
			// zero found
			if(one)
			{
				one = false;
				v = ~v; // invert all bits
			}
		}
		/* fall through */
		if(!(v & (1<<i)))
		{
			valid = false;
			break; // non-consistent netmask
		}
	}

	return valid;
}


std::vector<wifi_scan_item_t> get_wifi_scan_list(size_t max)
{
	// here we use naive method to sort the list.
	// assumes list is not so big.
	std::vector<wifi_scan_item_t> items;
	int16_t result = WiFi.scanComplete();
	if(result != WIFI_SCAN_FAILED && result > 0)
	{
		uint8_t num_stations = result;
		items.reserve(std::min((size_t)num_stations, max));
		for(int rssi = 0; rssi >= -100; --rssi)
		{
			for(uint8_t i = 0; i < num_stations; ++i)
			{
				if(WiFi.RSSI(i) == rssi)
				{
					// push an item
					uint8_t * bssid;
					bssid = WiFi.BSSID(i);
					items.push_back (wifi_scan_item_t {
						.SSID = WiFi.SSID(i),
						.encryptionType = WiFi.encryptionType(i),
						.RSSI = rssi,
						.BSSID = {bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5]},
						.BSSIDstr = WiFi.BSSIDstr(i),
						.channel = WiFi.channel(i),
					});
					if(items.size() == max) goto quit;
				}
			}
		}
	}
quit:
	return items;
}


const String null_ip_addr = "0.0.0.0";