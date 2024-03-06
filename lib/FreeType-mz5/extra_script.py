Import("env")
import os
import pathlib
import sys


# Inject dependency for mz_ftoption.h and mz_ftmodule.h .
# I don't know the right way to do this.

# Compare timestamp of mz_ft*.h and freetype/include/freetype/freetype.h,
# if mz_f*.h is newer, touch freetype/include/freetype/freetype.h.
my_config = "mz_ftoption.h"
my_module = "mz_ftmodule.h"
freetype_h = "freetype/include/freetype/freetype.h"

freetype_h_exists = os.path.exists(freetype_h)

if not freetype_h_exists:
    print(f"{freetype_h} missing.")
    print(f"Try 'git submodule update' at directory '{os.path.abspath(os.path.curdir)}'.", file = sys.stderr)
    exit(3)


my_config_time = os.stat(my_config).st_mtime
my_module_time = os.stat(my_module).st_mtime
if freetype_h_exists:
    freetype_h_time = os.stat(freetype_h).st_mtime

if (not freetype_h_exists or
    freetype_h_time <= my_config_time or
    freetype_h_time <= my_module_time):
    pathlib.Path(freetype_h).touch()


# use timestamp decider
env.Decider('timestamp-newer')