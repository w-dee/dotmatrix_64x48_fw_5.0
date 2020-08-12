#include <functional>
#include <vector>
#include "mz_console.h"
#include "mz_wifi.h"
#include "esp_console.h"
#include "esp_system.h"
#include "argtable3/argtable3.h"
#include "threadsync.h"
#include "settings.h"



// command handlers --------------------------------------------------
// command handlers should be contained in each namespace,
// to ease access to the static arg_XXX * items.




namespace cmd_wifi_show
{
    struct arg_lit *help = arg_litn(NULL, "help", 0, 1, "Display help and exit");
    struct arg_lit *i_am_safe = arg_litn(nullptr, "i-am-safe", 0, 1, "Show non-masked PSK (password)");
    struct arg_end *end = arg_end(5);
    void * argtable[] = { help, i_am_safe, end };

    static void show_ip_configuration()
    {
        ip_addr_settings_t settings = wifi_get_ip_addr_settings(true);
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        printf("--- current IP status ---\n");
        printf("WiFi self MAC    : %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        settings.dump("(not configured)");
        printf("--- configured IP settings ---\n");
        settings = wifi_get_ip_addr_settings(false);
        settings.dump("(use DHCP)");
    }

    static void show_ap_configuration(bool safe)
    {
        printf("--- AP settings ---\n");
        printf("SSID             : %s\n", wifi_get_ap_name().c_str());
        printf("PSK              : %s\n", 
            (safe ? wifi_get_ap_pass().c_str() : "******** (try --i-am-safe to show)"));
    }


    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-show", "Display WiFi status", argtable) {}

    private:
        int func(int argc, char **argv)
        {
            return run_in_main_thread([] () -> int {
                printf("Status: %s\n", wifi_get_connection_info_string().c_str());
                show_ip_configuration();
                 show_ap_configuration(i_am_safe->count > 0);
                return 0;
            }) ;       
        }
    };
}

namespace cmd_wifi_ip
{
    struct arg_lit *help, *dhcp;
    struct arg_str *address, *gw, *mask, *dns;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(NULL, "help", 0, 1, "Display help and exit"),
            dhcp =    arg_litn("d",  "dhcp", 0, 1, "Use DHCP"),
            address = arg_strn("a",  "addr", "<v4addr>", 0, 1, "IPv4 address"),
            gw =      arg_strn("g",  "gw",   "<v4addr>", 0, 1, "IPv4 gateway"),
            mask =    arg_strn("m",  "mask", "<v4mask>", 0, 1, "IPv4 mask"),
            dns =     arg_strn("n",  "dns",  "<v4addr>", 0, 2, "IPv4 DNS server"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-ip", "Set DCHP mode or IP addresses manually", argtable) {}

    private:
        static void set_dns(ip_addr_settings_t &settings)
        {
            if(dns->count > 0)
            {
                settings.dns1 = null_ip_addr;
                settings.dns2 = null_ip_addr;
                settings.dns1 = dns->sval[0];
                if(dns->count > 1)
                    settings.dns2 = dns->sval[1];
            }
        }

        static bool validate_ipv4(arg_str * arg, bool (*validator)(const String &), const char * label)
        {
            auto count = arg->count;
            for(typeof(count) i = 0; i < count; ++i)
            {
                if(!validator(arg->sval[i]))
                {
                    printf("Invalid IPv4 %s : '%s'\n", label, arg->sval[i]);
                    return false;
                }
            }
            return true;
        }

        int func(int argc, char **argv)
        {
            return run_in_main_thread([this, argc] () -> int {
                if(argc == 1)
                {
                    // nothing specified
                    // show current configuration
                    cmd_wifi_show::show_ip_configuration();
                    return 0;
                }
                const char *label_address = "address";
                const char *label_mask = "net mask";
                if(!validate_ipv4(address, validate_ipv4_address, label_address)) return 1;
                if(!validate_ipv4(gw, validate_ipv4_address, label_address)) return 1;
                if(!validate_ipv4(mask, validate_ipv4_netmask, label_mask)) return 1;
                if(!validate_ipv4(dns, validate_ipv4_address, label_address)) return 1;
                if(dhcp->count > 0)
                {
                    // use DHCP
                    if(address->count || gw->count || mask->count)
                    {
                        printf("DHCP mode can not specify address/gateway/mask.\n");
                        return 1;
                    }

                    ip_addr_settings_t settings = wifi_get_ip_addr_settings();
                    settings.ip_addr = null_ip_addr;
                    settings.ip_gateway = null_ip_addr;
                    settings.ip_mask = null_ip_addr;
                    if(dns->count > 0)
                        set_dns(settings); // use specified DNS
                    else
                        settings.dns1 = settings.dns2 = null_ip_addr; // otherwise use DHCP'ed DNS
                    wifi_manual_ip_info(settings);
                }
                else
                {
                    // manual ip settings
                    ip_addr_settings_t settings = wifi_get_ip_addr_settings();
                    if(address->count) settings.ip_addr = address->sval[0];
                    if(gw->count) settings.ip_gateway = gw->sval[0];
                    if(mask->count) settings.ip_mask = mask->sval[0];
                    set_dns(settings);
                    wifi_manual_ip_info(settings);
                }
                 return 0;
             }) ;
        }
    };
}

namespace cmd_wifi_ap
{
    struct arg_lit *help, *i_am_safe;
    struct arg_str *ssid, *psk;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(NULL, "help", 0, 1, "Display help and exit"),
            ssid =    arg_strn("s",  "ssid",   "<SSID>", 0, 1, "SSID name"),
            psk =     arg_strn("p",  "psk",    "<password>", 0, 1, "PSK (password)"),
            i_am_safe = arg_litn(nullptr, "i-am-safe", 0, 1, "Show non-masked PSK (password)"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-ap", "Set AP's SSID and PSK(password)", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            if(!ssid->count && !psk->count)
            {
                cmd_wifi_show::show_ap_configuration(i_am_safe->count > 0);
                return 0;
            }

            if(!(ssid->count && psk->count))
            {
                printf("Both SSID and PSK must be given.\n");
                return 1;
            }

            return run_in_main_thread([] () -> int {
                wifi_set_ap_info(ssid->sval[0], psk->sval[0]);
                return 0;
            }) ;       
         }
    };
}

namespace cmd_wifi_scan
{
    struct arg_lit *help;
    struct arg_lit *hidden, *active;
    struct arg_int *num;
    struct arg_end *end;
    void * argtable[] = {
            help =     arg_litn(NULL, "help", 0, 1, "Display help and exit"),
            num =      arg_intn("m", nullptr,  "<1-255>", 0, 1, "Maximum network count to show (default 20)"),
            hidden =   arg_litn("h", "hidden", 0, 1, "Show also hidden networks"),
            active =   arg_litn("a", "active", 0, 1, "Use active scan instead of passive scan"),
            end =      arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-scan", "Scan WiFi networks", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            size_t max = SIZE_MAX;
            if(num->count > 0)
            {
                if(num->ival[0] < 1 || num->ival[0] > 255)
                {
                    printf("Invalid count of -m option: %d\n", num->ival[0]);
                    return 1;
                }
                max = num->ival[0];
            }
            run_in_main_thread([] () -> int {
                // initiate scan
                WiFi.scanNetworks(true, hidden->count > 0, active->count > 0);
                return 0;
            }) ;
            // scan is running ... wait it done
            printf("Scanning ");
            fflush(stdout);
            while(run_in_main_thread([] () -> int {
                return WiFi.scanComplete() == WIFI_SCAN_RUNNING;
            }))
            {
                vTaskDelay(200);
                putchar('.');
                fflush(stdout);
            }
            puts("");
            // show the result
            return run_in_main_thread([&] () -> int {
                
                int16_t result = WiFi.scanComplete();
                if(result == WIFI_SCAN_FAILED)
                {
                    printf("WiFi scan failed.\n");
                    return 3;
                }
                if(result == 0)
                {
                    printf("No network found.\n");
                    return 0;
                }
                if((uint8_t)result != result)
                {
                    printf("Too many stations.\n");  // !!?!!!?!?!?!?
                    return 0;
                }
                printf("%-20s %-17s %4s %2s\n", "SSID", "BSSID", "RSSI", "Ch");
                std::vector<wifi_scan_item_t> items = get_wifi_scan_list(max);
                for(auto && i : items)
                {
                    printf("%-20s %17s %4d %2d\n",
                        i.SSID.c_str(),
                        i.BSSIDstr.c_str(),
                        i.RSSI,
                        i.channel
                        );
                }

                return 0;
            }) ;
         }
    };
}

namespace cmd_reboot
{
    struct arg_lit *help;
    struct arg_lit *hard;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(nullptr, "help", 0, 1, "Display help and exit"),
            hard =    arg_litn(nullptr, "clear-all-settings", 0, 1, "Clear all settings to their default values"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("reboot", "Reboot and optionally clear all settings", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            return run_in_main_thread([] () -> int {
                if(hard->count)
                {
                    // put all settings clear inidication file
                    FILE * f = fopen(CLEAR_SETTINGS_INDICATOR_FILE, "w");
                    if(f) fclose(f);
                }
                // ummm...
                // As far as I know, spiffs is always-consistent,
                // so at any point, rebooting the hardware may not corrupt
                // the filesystem. Obviously FAT is not. Take care if
                // using micro SD cards.
                printf("Rebooting ...\n");
                delay(1000);
                ESP.restart();
                for(;;) /**/ ;
                return 0; // must be non-reachable
            }) ;       
         }
    };
}



namespace cmd_t
{
    struct arg_lit *help = arg_litn(NULL, "help", 0, 1, "Display help and exit");
    struct arg_end *end = arg_end(5);
    void * argtable[] = { help, end };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("t", "Try to enable line edit / history", argtable) {}

    private:
        int func(int argc, char **argv)
        {
            return run_in_main_thread([] () -> int {
                console_probe();
                return 0;
            }) ;       
        }
    };
}

/**
 * Initialize console commands.
 * This must be called after other static variable initialization,
 * before using the command line interpreter.
 * */
void init_console_commands()
{
    static cmd_wifi_show::_cmd wifi_show_cmd;
    static cmd_wifi_ip::_cmd wifi_ip_cmd;
    static cmd_wifi_ap::_cmd wifi_ap_cmd;
    static cmd_wifi_scan::_cmd wifi_scan_cmd;
    static cmd_reboot::_cmd reboot_cmd;
    static cmd_t::_cmd t_cmd;
}