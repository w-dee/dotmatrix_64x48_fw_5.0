#include <Arduino.h>
#include <WiFi.h>
#include "matrix_drive.h"
//#include SSID_H
#include "settings.h"
#include "wifi.h"
#include "esp_wps.h"

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
  config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
  config.wps_type = ESP_WPS_MODE;
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

static void WiFiEvent(WiFiEvent_t event, system_event_info_t info){
  switch(event){
    case SYSTEM_EVENT_STA_START:
      Serial.println("Station Mode Started");
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("Connected to :" + String(WiFi.SSID()));
      Serial.print("Got IP: ");
      Serial.println(WiFi.localIP());
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
      Serial.println("WPS Successfull, stopping WPS and connecting to: " + String(WiFi.SSID()));
      esp_wifi_wps_disable();

		delay(10);
		// read WPS configuration regardless of WPS success or not
		ap_name = WiFi.SSID();
		ap_pass = WiFi.psk();

		// WPS client should be have automatic DHCP
		ip_addr_settings.clear();

		// write current configuration
		wifi_write_settings();

      WiFi.begin();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_FAILED:
      Serial.println("WPS Failed.");
      esp_wifi_wps_disable();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
      Serial.println("WPS Timedout.");
      esp_wifi_wps_disable();
      break;
    case SYSTEM_EVENT_STA_WPS_ER_PIN:
      Serial.println("WPS_PIN = " + wpspin2string(info.sta_er_pin.pin_code));
      break;
    default:
      break;
  }
	wifi_check();
}


static void wifi_init_settings();
void wifi_setup()
{
	Serial.println(F("WiFi initializing ..."));

	// check settings fs
	wifi_init_settings();

	// register event handler
	WiFi.onEvent(WiFiEvent);

	// first, disconnect wifi
	WiFi.mode(WIFI_OFF);
	WiFi.setAutoReconnect(true);

	// try to connect
	WiFi.mode(WIFI_STA);
	wifi_start();

}




static int wifi_last_status = -1;
void wifi_check()
{
	// output to console
	int st = WiFi.status();
	if(wifi_last_status != st)
	{
		wifi_last_status = st;
		Serial.print('\r');
		Serial.print('\n');
		String m = wifi_get_connection_info_string();
		Serial.print(m.c_str());
		Serial.print('\r');
		Serial.print('\n');
	}
}

ip_addr_settings_t::ip_addr_settings_t()
{
	clear();
}

void ip_addr_settings_t::clear()
{
	ip_addr_settings.ip_addr =
	ip_addr_settings.ip_gateway =
	ip_addr_settings.ip_mask =
	ip_addr_settings.dns1 =
	ip_addr_settings.dns2 = F("0.0.0.0");
}


/**
 * Begins WPS configuration.
 * This function *does* return before WPS config ends
 */
void wifi_wps()
{
	wpsInitConfig();
	esp_wifi_wps_enable(&config);
	esp_wifi_wps_start(0);
}

/**
 * Start WiFi connection using configured parameters
 */
void wifi_start()
{
	Serial.println(F("Starting WiFi ..."));

	WiFi.mode(WIFI_OFF);
	WiFi.setAutoReconnect(true);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ap_name.c_str(), ap_pass.c_str());

	IPAddress i_ip_addr;    i_ip_addr   .fromString(ip_addr_settings.ip_addr);
	IPAddress i_ip_gateway; i_ip_gateway.fromString(ip_addr_settings.ip_gateway);
	IPAddress i_ip_mask;    i_ip_mask   .fromString(ip_addr_settings.ip_mask);
	IPAddress i_dns1;       i_dns1      .fromString(ip_addr_settings.dns1);
	IPAddress i_dns2;       i_dns2      .fromString(ip_addr_settings.dns2);

	WiFi.config(i_ip_addr, i_ip_gateway, i_ip_mask, i_dns1, i_dns2);
}

/**
 * Read settings. Initialize settings to factory state, if the settings key is invalid
 */
static void wifi_init_settings()
{
	settings_write(F("ap_name"), F(""), SETTINGS_NO_OVERWRITE);
	settings_write(F("ap_pass"), F(""), SETTINGS_NO_OVERWRITE);
	settings_write(F("ip_addr"), F("0.0.0.0"), SETTINGS_NO_OVERWRITE); // automatic ip configuration
	settings_write(F("ip_gateway"), F("0.0.0.0"), SETTINGS_NO_OVERWRITE);
	settings_write(F("ip_mask"), F("0.0.0.0"), SETTINGS_NO_OVERWRITE);
	settings_write(F("dns_1"), F("0.0.0.0"), SETTINGS_NO_OVERWRITE);
	settings_write(F("dns_2"), F("0.0.0.0"), SETTINGS_NO_OVERWRITE);

	settings_read(F("ap_name"), ap_name);
	settings_read(F("ap_pass"), ap_pass);
	settings_read(F("ip_addr"),    ip_addr_settings.ip_addr);
	settings_read(F("ip_gateway"), ip_addr_settings.ip_gateway);
	settings_read(F("ip_mask"),    ip_addr_settings.ip_mask);
	settings_read(F("dns_1"),      ip_addr_settings.dns1);
	settings_read(F("dns_2"),      ip_addr_settings.dns1);
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
	settings_write(F("dns_2"),      ip_addr_settings.dns1);
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
			m += String(F(", IP address is ")) + WiFi.localIP().toString() + F(" .");
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
