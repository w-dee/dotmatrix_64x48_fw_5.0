#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_FILE "BA_64x48.raw"
#define OUTPUT_FILE "BA_64x48_comp.bin"

static void decode_test()
{
    printf("checking integrity...\n");
    FILE *org = fopen(INPUT_FILE, "rb");
    FILE *in = fopen(OUTPUT_FILE, "rb");
    if(!org || !in) exit(3);

    // retrieve input file size
    fseek(in, 0, SEEK_END);
    size_t length = ftell(in) - 12; // 12 = magic length + data size
    fseek(in, 12, SEEK_SET);

    // malloc and read
    printf("length : %ld\n", (long)length);
    uint8_t *buf = (uint8_t*)malloc(length);
    if(!buf) exit(3);
    if(1 != fread(buf, length, 1, in)) exit(3);

    // do loop
    uint8_t *ptr = buf;
    int frame = 1;
    while(1)
    {
        printf("\rprocessing frame %d ...", frame);

        uint8_t org_buf[64*48];
        uint8_t exp_buf[64*48];
        // read the original
        if(1 != fread(org_buf, 64*48, 1, org)) break;

        // expand the buffer
        int out_pos = 0;
        while(out_pos < 64*48)
        {
            if(*ptr & 0x80)
            {
                // running
                uint8_t len = *ptr & 0x7f;
                ++ptr;
                uint8_t val = *ptr;
                ++ptr;
                while(len--)
                    exp_buf[out_pos++] = val;
            }
            else
            {
                // non-running
                uint8_t len = *ptr;
                ++ptr;
                while(len--)
                    exp_buf[out_pos++] = *(ptr++);
            }
        }
        if(out_pos != 64*48)
        {
            printf("decompress failed: out_pos: %d, expected %d\n",
                out_pos, 64*48);
            exit(3);
        }

        if(memcmp(org_buf, exp_buf, sizeof(exp_buf)))
        {
            printf("mismatch!\n");
            exit(3);
        }

        ++ frame;
    }
    free(buf);
    fclose(org);
    fclose(in);
    printf("\nall ok\n");
}


int main(void)
{
    FILE *in = fopen(INPUT_FILE, "rb");
    FILE *out = fopen(OUTPUT_FILE, "wb");
    if(!in || !out) return 3;

    if(1 != fwrite("BADAPPLE\0    ", 12, 1, out)) return 3; // write magic

    while(1)
    {
        uint8_t buf[64*48];
        int input_pos = 0;

        if(1 != fread(buf, 64*48, 1, in)) break;

        // simple run-length encoding

        while(input_pos < 64*48)
        {
            // scan from the current point.
            int running_count = 1;
            int non_running_count = 1;
            uint8_t cur = buf[input_pos];
            for(int ptr = input_pos + 1; ptr < 64*48 && running_count < 127; ++ptr)
            {
                if(buf[ptr] == cur) ++ running_count; else break; 
            }
            for(int ptr = input_pos + 1; ptr < 64*48 && non_running_count < 127; ++ptr)
            {
                // TODO: allow one or two running value to get more efficiency
                if(buf[ptr] != cur) ++ non_running_count; else break;
                cur = buf[ptr];
            }
            if(non_running_count > running_count)
            {
                // non_running phase
                // write non running length and ..
                putc(non_running_count, out);
                // write a string of non-running values
                fwrite(buf + input_pos, non_running_count, 1, out);
                input_pos += non_running_count;
            }
            else
            {
                // running phase
                // write running length with a flag and ..
                putc(running_count | 0x80, out);
                // write running value
                putc(cur, out);
                input_pos += running_count;
            }
        }
    }


    // before close, write file size at offset 8
    uint32_t size = (uint32_t)ftell(out);
    fseek(out, 8, SEEK_SET);
    fputc((size >>  0) & 0xff, out);
    fputc((size >>  8) & 0xff, out);
    fputc((size >> 16) & 0xff, out);
    fputc((size >> 24) & 0xff, out);
    fclose(in);
    fclose(out);

    decode_test();
}


