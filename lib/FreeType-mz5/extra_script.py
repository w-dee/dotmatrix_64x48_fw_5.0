Import("env")
import os
import pathlib


# Inject dependency for mz_ftoption.h and mz_ftmodule.h .
# I don't know the right way to do this.

# Compare timestamp of mz_ft*.h and freetype/include/freetype/freetype.h,
# if mz_f*.h is newer, touch freetype/include/freetype/freetype.h.
my_config = "mz_ftoption.h"
my_module = "mz_ftmodule.h"
freetype_h = "freetype/include/freetype/freetype.h"


my_config_time = os.stat(my_config).st_mtime
my_module_time = os.stat(my_module).st_mtime
freetype_h_time = os.stat(freetype_h).st_mtime

if freetype_h_time <= my_config_time or freetype_h_time <= my_module_time:
    pathlib.Path(freetype_h).touch()


# use timestamp decider
env.Decider('timestamp-newer')