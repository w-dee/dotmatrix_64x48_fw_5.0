Import("env")
import make_archive

# upload helper function
def extra_upload(address, filename):
    env.Replace(FONT_ADDRESS=address, FONT_FILE_NAME=filename)
    env.AutodetectUploadPort()
    env.Replace(
        UPLOADERFLAGS=[
            "--chip", "esp32",
            "--port", '"$UPLOAD_PORT"',
            "--baud", "$UPLOAD_SPEED",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash", "-z",
            "--flash_mode", "$BOARD_FLASH_MODE",
            "--flash_size", "detect",
            "$FONT_ADDRESS", "$FONT_FILE_NAME"
        ],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $SOURCE' )
    env.Execute(env["UPLOADCMD"]) 

# custom target "uploadba" to upload bad apple movie demo
def uploadba(*args, **kwargs):
    # note: keep that this font start address and the address written in custom.csv are in sync.
    # note: this may destory font partition.
    extra_upload("0x890000", "work-priv/BadApple/BA_64x48_comp.bin")

# custom target "uploadfont" to upload font file
def uploadfont(*args, **kwargs):
    # note: keep that this font start address and the address written in custom.csv are in sync.
    # TODO: take the address automatically from the csv file
    extra_upload("0x890000", "src/fonts/TakaoPGothicC.ttf")


env.AddCustomTarget(name="uploadfont",
    actions=uploadfont,
    dependencies=None,
    title="upload font",
    description="upload font partition")

env.AddCustomTarget(name="uploadba",
    actions=uploadba,
    dependencies=None,
    title="upload badapple",
    description="upload bad apple test movie")


def do_make_archive(*args, **kwargs):
    make_archive.do_make_archive()

env.AddCustomTarget(name="makearchive",
    dependencies=["$BUILD_DIR/${PROGNAME}.bin"],
    actions=do_make_archive, title="OTA archive",
    description="make firmware OTA archive")