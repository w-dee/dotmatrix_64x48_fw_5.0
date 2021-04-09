INPUT_FILE=TouhouBadApplePV_480p.mp4
INTERMEDIATE_FILE="BA_64x48.raw"
OUTPUT_FILE="BA_64x48_comp.bin"
WRITE_ADDRESS=0x890000


$(OUTPUT_FILE): compress $(INTERMEDIATE_FILE)
	./compress 


$(INTERMEDIATE_FILE): $(INPUT_FILE)
	ffmpeg -y -i $(INPUT_FILE) -s 64x48 -an -f rawvideo -pix_fmt gray  $(INTERMEDIATE_FILE)

compress: compress.c Makefile
	gcc -Wall -o compress -g -Og compress.c

write: $(OUTPUT_FILE)
