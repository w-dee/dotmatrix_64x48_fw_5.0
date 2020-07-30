#include <functional>
#include <vector>
#include "wifi.h"
#include "esp_console.h"
#include "esp_system.h"
#include "argtable3/argtable3.h"
#include "threadsync.h"


class cmd_base_t;
static std::vector<cmd_base_t*> commands;

static int generic_handler(int argc, char **argv);

class cmd_base_t
{
    const char * name;
    const char * hint;
    void ** at;

    virtual int func() = 0;

    int handler(int argc, char **argv)
    {
        int nerrors;
        nerrors = arg_parse(argc,argv,at);
        if(((struct arg_lit*)at[0])->count > 0) // at[0] must be help
        {
            printf("Usage: %s", name);
            arg_print_syntax(stdout, at, "\n");
            printf("%s.\n\n", hint);
            arg_print_glossary(stdout, at, "  %-25s %s\n");
            return 0;
        }

        if (nerrors > 0)
        {
            // search for arg_end
            int tabindex;
            struct arg_hdr ** table = (struct arg_hdr**)at;
            for (tabindex = 0; !(table[tabindex]->flag & ARG_TERMINATOR); tabindex++)
            {
                printf("%p: %d\n", table[tabindex], table[tabindex]->flag);
            }

            /* Display the error details contained in the arg_end struct.*/
            arg_print_errors(stdout, (struct arg_end*)(table[tabindex]), name);
            printf("Try '%s --help' for more information.\n", name);
            return 0;
        }

        return func();
    }

    bool is_you(const char *p) const
    {
        return !strcmp(name, p);
    }

public:
    cmd_base_t(const char *_name, const char *_hint, void ** const _argtable) :
        name(_name), hint(_hint), at(_argtable)
    {
        esp_console_cmd_t cmd;

        cmd.command = name;
        cmd.help = hint;
        cmd.hint = nullptr;
        cmd.func = generic_handler;
        cmd.argtable = at;

        esp_console_cmd_register(&cmd);

        commands.push_back(this);
    }


    friend int generic_handler(int argc, char **argv);
};

// because ESP-IDF's esp_console_cmd_func_t does not include
// any user pointer, there is no method to know from the
// handler that which command is invoked. Fortunately
// argv[0] contains command name itself
// so check the list. bullshit.
static int generic_handler(int argc, char **argv)
{
    for(auto && cmd : commands)
    {
        if(cmd->is_you(argv[0])) { return cmd->handler(argc, argv); }
    }
    return -1; // command not found
}


namespace cmd_wifi_show
{
    struct arg_lit *help = arg_litn(NULL, "help", 0, 1, "Display help and exit");
    struct arg_lit *i_am_safe = arg_litn(nullptr, "i-am-safe", 0, 1, "show non-masked PSK (password)");
    struct arg_end *end = arg_end(5);
    void * argtable[] = { help, i_am_safe, end };

    static void dump(const ip_addr_settings_t & settings, bool is_current)
    {
        printf("IPv4 address     : %s", settings.ip_addr.c_str());
        if(settings.ip_addr == "0.0.0.0")
        {
            if(is_current)
                printf(" (not yet DHCP'ed)");
            else
                printf(" (use DHCP)");
        }
        printf("\n");
        printf("IPv4 gateway     : %s\n", settings.ip_gateway.c_str());
        printf("IPv4 subnet mask : %s\n", settings.ip_mask.c_str());
        printf("IPv4 DNS serv 1  : %s\n", settings.dns1.c_str());
        printf("IPv4 DNS serv 2  : %s\n", settings.dns2.c_str());
    }

    class _cmd : public cmd_base_t
    {

    public:
        _cmd() : cmd_base_t("wifi-show", "display WiFi status", argtable) {}

    private:
        int func()
        {
            run_in_main_thread([] () {
                printf("%s\n", wifi_get_connection_info_string().c_str());
                ip_addr_settings_t settings = wifi_get_ip_addr_settings(true);
                printf("--- current IP status ---\n");
                dump(settings, true);
                printf("--- configured IP settings ---\n");
                settings = wifi_get_ip_addr_settings(false);
                dump(settings, false);
                printf("--- AP settings ---\n");
                printf("SSID             : %s\n", wifi_get_ap_name().c_str());
                printf("PSK              : %s\n", 
                    (i_am_safe->count > 0) ? wifi_get_ap_pass().c_str() : "******** (use --i-am-safe option to show)");

            }) ;
            return 0;         
        }
    };
}


void init_console_commands()
{
    {static cmd_wifi_show::_cmd cmd;}
}