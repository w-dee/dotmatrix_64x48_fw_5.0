#include <functional>
#include <vector>
#include "mz_console.h"
#include "mz_wifi.h"
#include "esp_console.h"
#include "esp_system.h"
#include "argtable3/argtable3.h"
#include "threadsync.h"
#include "settings.h"
#include "calendar.h"
#include "ir_rmt.h"
#include "buttons.h"
#include "mz_update.h"
#include "mz_version.h"


// wait for maximum 20ms, checking key type, returning
// whether any key has pressed(-1 = no keys pressed)
static int any_key_pressed()
{
    // check STDIN has anything to consume
    // say thanks to ESP32 IDF which successfully implemented
    // proper select().
    fd_set readfds;
    FD_ZERO(&readfds);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;
    FD_SET(STDIN_FILENO, &readfds);
    if (select(1, &readfds, NULL, NULL, &timeout))
    {
        // yes, something is there
        return getchar(); // eat it and return
    }
    return -1;
}




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
        _cmd() : cmd_base_t("wifi-ip", "Set DHCP mode or IP addresses manually", argtable) {}

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
           return run_in_main_thread([] () -> int {
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

namespace cmd_wifi_wps
{
    struct arg_lit *help, *push;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(NULL, "help", 0, 1, "Display help and exit"),
            push =    arg_litn("p",  "push", 0, 1, "Start WPS (Push Button Mode)"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-wps", "Start WPS", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            if(!push->count)
            {
                printf("To start WPS, try -p option.\n");
                return 0;
            }

            // start WPS
            printf("WPS initializing...\n");

            run_in_main_thread([] () -> int {
                wifi_wps();
                return 0;
            }) ;       

            printf("WPS started. Type any key to stop WPS now.\n");

            // check WPS status
            int last_status;
            while((last_status = run_in_main_thread([] () -> int {
                return (int) wifi_get_wps_status();
            })) == SYSTEM_EVENT_WIFI_READY)
            {
                if(any_key_pressed() != -1)
                {
                    printf("Stopping WPS.\n");
                    run_in_main_thread([] () -> int { wifi_stop_wps(); return 0; });
                    goto quit;
                }
            }

            switch(last_status)
            {
            case (int)SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
                printf("WPS succeeded.\n");
                break;
            case (int)SYSTEM_EVENT_STA_WPS_ER_FAILED:
                printf("WPS failed.\n");
                break;
            case (int)SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
                printf("WPS timed out.\n");
                break;
            default:
                printf("WPS failed by unknown reason.\n");
                break;
            }
        quit:
            return 0;
         }
    };
}

namespace cmd_ntp
{
    struct arg_lit *help = arg_litn(NULL, "help", 0, 1, "Display help and exit");
    struct arg_str *servers = arg_strn("s", "server", "<server>", 0, 3, "NTP servers");
    struct arg_str *time_zone = arg_strn("z", "time-zone", "<tz-spec>", 0, 1, "Time zone (eg. JST-9)");
    struct arg_end *end = arg_end(5);
    void * argtable[] = { help, servers, time_zone, end };

    static void show_ntp_configuration()
    {
        printf("--- NTP settings ---\n");
        string_vector time_servers;
        String time_zone_str;
        get_tz(time_servers, time_zone_str);
        // show three configurable time servers
        for(size_t i = 0; i < 3; ++i)
        {
            printf("NTP server %d     : %s\n", (int)(i+1),
                (time_servers.size() > i && !time_servers[i].isEmpty()) ? time_servers[i].c_str() :
                    "(unspecified)");
        }
        printf("Time zone        : %s\n", time_zone_str.c_str());
        char buf[30];
        time_t tm;
        tm = time(&tm);
        printf("Local time got   : %s", ctime_r(&tm, buf)); // ctime puts \n at its last of output
    }

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("ntp", "Set NTP servers and time zone", argtable) {}

    private:
        int func(int argc, char **argv)
        {
            return run_in_main_thread([] () -> int {
                if(!servers->count && !time_zone->count)
                {
                    // no option was given; show current configuration
                    show_ntp_configuration();
                    return 0;
                }

                string_vector time_servers;
                String time_zone_str;
                get_tz(time_servers, time_zone_str);

                // check parameters
                if(time_zone->count)
                {
                    if(strlen(time_zone->sval[0]) == 0)
                    {
                        printf("Empty time zone is invalid.\n");
                        return 1;
                    }
                    time_zone_str = time_zone->sval[0];
                }

                if(servers->count)
                {
                    // new servers are specified;
                    // clear all existing servers
                    time_servers.clear();
                    for(int i = 0; i < servers->count; ++i)
                    {
                        if(strlen(servers->sval[i]) > 0)
                        {
                            time_servers.push_back(servers->sval[i]);
                        }
                        else
                        {
                            printf("Empty time server is invalid.\n");
                            return 1;
                        }
                    }
                }

                // set new servers
                set_tz(time_servers, time_zone_str);
                return 0;
            });
        }
    };
}

namespace cmd_rmt
{
    struct arg_lit *help;
    struct arg_lit *recv;
    struct arg_int *num;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(NULL, "help", 0, 1, "Display help and exit"),
            recv =    arg_litn("r", nullptr, 0, 1, "Do receive"),
            num =     arg_int1(nullptr, nullptr, "<0-99>", "Data slot number"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("rmt", "IR remote control", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            char numstr[5];
            if(num->ival[0] < 0 || num->ival[0] > 99)
            {
                printf("Data slot number must be within 0 - 99.\n");
                return 1;
            }
            snprintf(numstr, sizeof(numstr), "%02d", num->ival[0]);

            if(!recv->count)
            {
                // send mode
                run_in_main_thread([&numstr] () -> int {
                        rmt_start_send(numstr + String(".rmt"));
                        return 0;
                    });
                while(true)
                {
                    if(any_key_pressed() != -1) break;
                    if(!run_in_main_thread([] () -> int { return rmt_in_progress(); })) break;
                }
                rmt_result_t result = (rmt_result_t)run_in_main_thread([] () -> int {
                    return rmt_get_status();
                });
                switch(result)
                {
                case rmt_notfound:
                    printf("Slot %s not found.\n", numstr);
                    break;
                case rmt_broken:
                    printf("Broken slot number %s.\n", numstr);
                    break;
                default:
                    break; // unknown state ...?
                }
                // clear rmt state
                run_in_main_thread([] () -> int {
                    rmt_clear();
                    return 0;
                });
                return 0;
            }
            else
            {
                // receive mode
                run_in_main_thread([] () -> int {
                    rmt_start_receive();
                    return 0;
                });
                printf("Waiting for an IR remote controller signal ...\n");
                printf("Press any key to stop.\n");
                while(true)
                {
                    if(any_key_pressed() != -1)
                    {
                        // stop receiving when any key is pressed
                        run_in_main_thread([] () -> int {
                            rmt_stop_receive();
                            return 0;
                        });
                        break;
                    }
                    if(!run_in_main_thread([] () -> int { return rmt_in_progress(); })) break;
                }

                rmt_result_t result = (rmt_result_t)run_in_main_thread([] () -> int {
                    return rmt_get_status();
                });
                switch(result)
                {
                case rmt_nomem:
                    printf("IR signal too complex.\n");
                    break;
                case rmt_interrupted:
                    printf("Interrupted.\n");
                    break;
                case rmt_done:
                    // done; save the data
                    run_in_main_thread([&numstr] () -> int {
                        rmt_save(numstr + String(".rmt"));
                        return 0;
                    });
                    break;
                default:
                    break; // unknown state ...?
                }
                // clear rmt state
                run_in_main_thread([] () -> int {
                    rmt_clear();
                    return 0;
                });
                return 0;
            }
            return 0;
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
                reboot(hard->count);
                return 0; // must be non-reachable
            }) ;       
         }
    };
}



namespace cmd_keys
{
    struct arg_lit *help;
    struct arg_end *end;
    void * argtable[] = {
            help =    arg_litn(nullptr, "help", 0, 1, "Display help and exit"),
            end =     arg_end(5)
            };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("keys", "Emulate physical button by terminal input", argtable) {}

    private:

        int func(int argc, char **argv)
        {
            printf("Key emulation:\n"
                "  W/K/UP      :  Up\n"
                "  A/H/LEFT    :  Left\n"
                "  D/L/RIGHT   :  Right\n"
                "  S/J/DOWN    :  Down\n");
            if(dumb_mode)
                printf("  SPACE       :  OK\n");
            else
                printf("  ENTER/SPACE :  OK\n");
            
            printf(""
                "  B/Z         :  CANCEL\n"
                "  Q/ESC       :  Quit this mode\n"
            );
            for(;;)
            {
                int ch = getchar();
                int ch2, ch3, ch4;
                uint32_t buttons = 0;
                switch(ch)
                {
                case 0x03: // ctrl + c
                    return 0; // quit

                case '\x1b': // escape
                    // wait for 200ms maximum and receive escaped
                    // characters
                    ch2 = -1;
                    for(int i = 0; i < 10; ++i)
                    {
                        ch2 = any_key_pressed(); // wait for 20ms
                        if(ch2 != -1) break;
                    }
                    if(ch2 == -1) return 0; // timed out; it was ESC key
                    if(ch2 == 0x1b) return 0; // ESC ESC is also ESC key
                    switch(ch2)
                    {
                    case 0x20: // S7C/S8C
                    case 0x23: // DHL/SWL/DWL/ALN
                    case 0x28: // SCS-0
                    case 0x29: // SCS-1
                    case 0x2a: // SCS-2
                    case 0x2b: // SCS-3
                    case 0x2d: // SCS-1A
                    case 0x2e: // SCS-2A
                    case 0x2f: // SCS-3A
                    case 0x4f: // App keypads
                    case 0x5b: // exotic keys
                        ch3 = getchar();
                        ch4 = -1;
                        if(ch2 == 0x5b)
                        {
                            switch(ch3)
                            {
                            case 0x30: // MC/0
                            case 0x34: // MC/4
                            case 0x35: // MC/5
                            case 0x31: // SM78
                            case 0x32: // SM77
                            case 0x33: // SM73
                            //case 0x34: // SM83
                            //case 0x35: // SM64
                                ch4 = getchar(); // four character escape seq
                                (void)ch4;
                                break;

                            case 0x41: // UP
                                buttons |= BUTTON_UP;
                                break;

                            case 0x42: // DOWN
                                buttons |= BUTTON_DOWN;
                                break;

                            case 0x43: // RIGHT
                                buttons |= BUTTON_RIGHT;
                                break;

                            case 0x44: // LEFT
                                buttons |= BUTTON_LEFT;
                                break;

                            default:;
                            }
                        }
                        break;
                    
                    default:
                        return 0; // unknown escape sequence byte followed immediately by ESC
                    }
                    break;

                case 'Q': case 'q': return 0; // quit
                    return 0; // quit

                case 'W': case 'w': 
                case 'K': case 'k':
                    buttons |= BUTTON_UP;
                    break;

                case 'A': case 'a': 
                case 'H': case 'h':
                    buttons |= BUTTON_LEFT;
                    break;

                case 'D': case 'd': 
                case 'L': case 'l':
                    buttons |= BUTTON_RIGHT;
                    break;

                case 'S': case 's': 
                case 'J': case 'j':
                    buttons |= BUTTON_DOWN;
                    break;

                case ' ': case 0x0a:
                    if(dumb_mode)
                    {
                        // in dumb mode, enter key may required
                        // to push input buffer into the serial
                        if(ch == ' ') buttons |= BUTTON_OK;
                    }
                    else
                    {
                        buttons |= BUTTON_OK;
                    }
                    
                    break;

                case 'B': case 'b': 
                case 'Z': case 'z': 
                    buttons |= BUTTON_CANCEL;
                    break;

                default:
                    break;
                }
                run_in_main_thread([buttons] () -> int {
                    button_push(buttons);
                    return 0;
                });

            }
            return 0;
        }
    };
}


namespace cmd_ver
{
    struct arg_lit *help = arg_litn(NULL, "help", 0, 1, "Display help and exit");
    struct arg_end *end = arg_end(5);
    void * argtable[] = { help, end };

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("ver", "Show version", argtable) {}

    private:
        int func(int argc, char **argv)
        {
            return run_in_main_thread([] () -> int {
                printf("%s\n", version_get_info_string().c_str());
                return 0;
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
    static cmd_wifi_wps::_cmd wifi_wps_cmd;
    static cmd_ntp::_cmd ntp_cmd;
    static cmd_rmt::_cmd rmt_cmd;
    static cmd_reboot::_cmd reboot_cmd;
    static cmd_keys::_cmd keys_cmd;
    static cmd_ver::_cmd ver_cmd;
    static cmd_t::_cmd t_cmd;
}