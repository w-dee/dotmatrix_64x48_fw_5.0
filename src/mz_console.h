#pragma once



/**
 * base class for command handler
 * */
class cmd_base_t
{
    const char * name;
    const char * hint;
    void ** at;

    virtual int func(int argc, char **argv) = 0;

public:
    /**
     * Show usage
     * */
    void usage() const;

public:
    /**
     * Common command handler
     * */
    int handler(int argc, char **argv);

public:
    /**
     * The constructor.
     * _argtable is an argtable array. The first item must be the help.
     * */
    cmd_base_t(const char *_name, const char *_hint, void ** const _argtable);


    /**
     * returns the command name matches the argument
     * */
    bool are_you(const char* _name) const;

    /**
     * returns hint
     * */
    const char * get_hint() const { return hint;}

    /**
     * returns name
     * */
    const char * get_name() const { return name; }

};

/**
 * Check whether the connected terminal is capable of line editing.
 * This needs the terminal is actually connected and can bi-directionally
 * communicable with the ESP32 at the time that this function is called.
 * */
void console_probe();

/**
 * Initialize console and command line interpreter facility.
 * This also initialize stdout/stdin so printf over the serial line can work.
 * */
void init_console();

void flush_stdout();

/**
 * This must be called after settings filesystem becoming ready.
 * */
void begin_console();

/**
 * indicates whether the current input mode is dumb terminal mode(true) or rich terminal mode(false)
 * */
extern bool dumb_mode;