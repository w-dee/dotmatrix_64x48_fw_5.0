#ifndef SETTINGS_H__
#define SETTINGS_H__

#include <vector>

typedef std::vector<String> string_vector;

const char CLEAR_SETTINGS_INDICATOR_FILE[] = "/fs/.clear"; // a file indicated by its existance, to clear all settings at boot

static constexpr size_t MAX_SETTINGS_TAR_SIZE = 32*1024;

struct settings_overwrite_t { bool overwrite;  };
#define SETTINGS_NO_OVERWRITE settings_overwrite_t{false}
#define SETTINGS_OVERWRITE settings_overwrite_t{true}

/**
 * Initialize settings facility.
 * */
void init_settings();


void _clear_settings();

bool settings_write(const String & key, const void * ptr, size_t size, settings_overwrite_t overwrite = SETTINGS_OVERWRITE);
bool settings_write(const String & key, const String & value, settings_overwrite_t overwrite = SETTINGS_OVERWRITE);
bool settings_read(const String & key, void *ptr, size_t size);
bool settings_read(const String & key, String & value);


bool settings_write_vector(const String & key, const string_vector & value, settings_overwrite_t overwrite = SETTINGS_OVERWRITE);
bool settings_read_vector(const String & key, string_vector & value);


bool settings_export(const String & target_name,
	const String & exclude_prefix);
bool settings_import(const String & target_name);

#endif
