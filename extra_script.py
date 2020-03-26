Import("env")

# custom target "uploadfont" to upload font file
def uploadfont(*args, **kwargs):
    # note: keep that this font start address and the address written in custom.csv are in sync.
    # TODO: take the address automatically from the csv file
    env.Replace(FONT_ADDRESS="0x290000", FONT_FILE_NAME="src/fonts/takaop.bff")
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

env.AlwaysBuild(env.Alias("uploadfont", None, uploadfont))

