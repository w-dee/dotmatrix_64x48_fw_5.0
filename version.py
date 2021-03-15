import datetime
import os
import subprocess

Import("env")

result = subprocess.run(['git', 'describe', '--tags', '--always', '--dirty'], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()



VERSION_CONTENTS = """
const char * get_git_rev() {{
 return "{}";
}}
const char * get_build_date() {{
 return "{}";
}}
""".format(result, datetime.datetime.now())

with open("src/mz_build_version.c", 'w+') as FILE:
        FILE.write(VERSION_CONTENTS)
