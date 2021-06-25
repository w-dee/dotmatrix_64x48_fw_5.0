#!/usr/bin/env python3

import os
import subprocess
import hashlib
import struct
import zlib
from io import BytesIO


def bin_padding(bin, size):
    size = ((len(bin) -1) // size + 1) * size
    return struct.pack(f"<{size}s", bin)

def do_make_archive():
    pio_env_name = "esp32dev"
    pio_build_dir = f".pio/build/{pio_env_name}"

    files = [
        ["src/fonts/TakaoPGothicC.ttf", "font"],
        [f"{pio_build_dir}/spiffs.bin", "spiffs"],
        [f"{pio_build_dir}/firmware.bin", "app"] # the firmware must be the last
    ]

    sector_size = 4096

    # execute spiffs binary generation (TODO: proper scons execution)
    res = subprocess.call(f"pio run --target buildfs --environment {pio_env_name}", shell=True)
    if(res != 0):
        print("Could not run pio command. Check the pio installation.\n")
        exit(3)

    # open the binary memory stream
    stream = BytesIO()

    # write archive header
    stream.write(bin_padding(b"MZ5 firmware archive 1.0\r\n\n\x1a    ", sector_size))

    # iterate into file list
    for file in files:
        filename = file[0]
        label = file[1]

        # open the input file
        f = open(filename, "rb")

        # read all content
        content = f.read()
        content_org_len = len(content)
        content = bin_padding(content, sector_size)
        content_arc_len = len(content)

        # get hash
        hash_bin = hashlib.md5(content).digest()

        # write archive file header
        header = (b"-file boundary--" +
            struct.pack("<8sLL", label.encode('utf-8'), content_org_len, content_arc_len) +
            hash_bin)
        stream.write(bin_padding(header, sector_size))

        # write content
        stream.write(bin_padding(content, sector_size))

    # compress and write the output file
    outfn = f".pio/build/{pio_env_name}/mz5_firm.bin"
    out = open(outfn, "wb")
    out.write(b"MZ5 compressed archive\r\n\n\x1a")
    compressed = zlib.compress(stream.getvalue(), 3)
    out.write(struct.pack("<L", len(compressed)))
    out.write(compressed)

    # done
    print(F"Made OTA archive at {outfn}\n")

if __name__ == '__main__':
    do_make_archive()
