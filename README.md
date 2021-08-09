# Build instruction

First, you need a PlatformIO installation.

The official site: https://platformio.org/

You will find a bunch of sites that help you to install the PlatformIO.

Optionally you can install PlatformIO as an extension of VS Code.

You will also need git installation.

## Cloning the source

Go to your favorite project folder. Then:

    $ git clone git@github.com:w-dee/dotmatrix_64x48_fw_5.0.git --recursive

Go to the cloned folder:

    $ cd dotmatrix_64x48_fw_5.0/

Did you forget to specify "--recursive" to "git clone" command ? Then at your cloned folder:

    (at your cloned folder)$ git submodule update --init

## Build

At your cloned folder:

    (at your cloned folder)$ pio run

This command will automatically download dependent toolchains and libraries.

## Upload

Connect the hardware with a USB cable and DC power adapter, then at your cloned folder:

    (at your cloned folder)$ pio run -t upload

The command above only upload a "code" partition. Since MZ5 firmware also uses "fs"(filesystem) and "font" partition, you will need to run following commands:

    (at your cloned folder)$ pio run -t uploadfs
    (at your cloned folder)$ pio run -t uploadfont

But the contents of these partition are rarely changed, in most situation you will need to do only upload "code" partition.

# OTA

The OTA (over the air) update can be performed on the web interface.

## Build OTA archive

    (at your cloned folder)$ pio run -t makearchive

Then you will get ".pio/build/esp32dev/mz5_firm.bin", the OTA firmware archive. The archive includes all the partition (code, fs, fonts) needed.

Or, if you are using pre-release firmware (mostly if you are beta test user), you will need the old style archive file ".pio/build/esp32dev/mz5_firm.bin.uncompressed". Try this when the OTA fails if you are using older firmware.

## Do the OTA upload

Navigate to

    http://[IP ADDRESS of the MZ5]/

or

    http://mz5.local/

if you can use mDNS (most Linux, Windows 10 or Mac OS X).

The default username is admin , and the password is admin .

Click "Firmware Upgrade", then specify the OTA firmware archive.

Press "Update" and wait for the process done.






