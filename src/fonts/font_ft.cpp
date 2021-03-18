#include <stdlib.h>
#include "font_ft.h"
#include <esp_partition.h>
#include "mz_update.h"
#include "frame_buffer.h"
#include "freetype/internal/ftdebug.h"

static FT_Library library; // the FT library instance

/**
 * initialize FT library
 * */
static void init_freetype()
{
    if(!library)
    {
        auto error = FT_Init_FreeType(&library);
        if(error)
        {
            // TODO: panic
            printf("font_ft: WTF!?\n");
        }
    }
}

// global FT instance
ft_font_t font_ft;


ft_font_t::ft_font_t() : face(nullptr)
{
}


void ft_font_t::begin()
{
    unsigned long fre = xPortGetFreeHeapSize();
    printf("Memory free area before FreeType font load: %ld\n", fre);
    FT_Trace_Enable();
    setenv("FT2_DEBUG", "any:7", 1);
    _begin();
    unsetenv("FT2_DEBUG");
    FT_Trace_Disable();
    unsigned long fre_a = xPortGetFreeHeapSize();
    printf("Memory free area after FreeType font load: %ld, %ld bytes consumed.\n", fre_a, fre - fre_a);;
}

void ft_font_t::_begin()
{
    init_freetype();

    // find font partition and mmap into the address space
	puts("font_ft: TrueType font initializing ...");
    const esp_partition_t * part =
        esp_partition_find_first((esp_partition_type_t)0x40,
        (esp_partition_subtype_t)get_current_active_partition_number(), NULL);
    if(part == nullptr)
    {
        // TODO: panic
        printf("font_ft: No TrueType font partitions found!\n");
        return;
    }

    printf("TrueType font partition start: 0x%08x, mapped to: ", part->address);

    const void *map_ptr;
    spi_flash_mmap_handle_t map_handle;
    if(ESP_OK != esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &map_ptr, &map_handle))
    {
        // TODO: panic
        printf("font_ft: TrueType mmap() failed.\n");
        return;
    }

    const uint8_t * ptr = static_cast<const uint8_t *>(map_ptr);
    printf("%p\r\n", ptr);
    printf("font_ft: TrueType Font data magic: %02x %02x %02x %02x\r\n", ptr[0], ptr[1], ptr[2], ptr[3]);


    // attempt to open with freetype
    // ??? FT needs the data size !?? I was not aware of that ...
    auto error = FT_New_Memory_Face(library, ptr, part->size, 0, &face);
    if(error)
    {
        printf("TrueType open failed: %d\n", (int)error);
        return;
    }

    printf("font_ft: TrueType font file opened successfully:");
    printf("num_faces:%ld, num_glyphs:%ld, family_name:%s, style_name:%s\n",
        face->num_faces, face->num_glyphs, face->family_name, face->style_name);


    error = FT_Set_Pixel_Sizes(face, 0, GLYPH_HEIGHT_PX);
    if(error)
    {
        // no way 
        printf("font_ft: FT_Set_Pixel_Sizes failed.\n");
        FT_Done_Face(face);
        face = nullptr;
    }
}

ft_font_t::~ft_font_t() // will not called
{
    FT_Done_Face(face); // will not called
}


ft_font_t::metrics_t ft_font_t::get_metrics(int32_t chr) const
{
    auto index = FT_Get_Char_Index(face, chr);
    if(!index)
    {
        // undefined character code
        return {0,0,false}; // non existent
    }
    auto error = FT_Load_Glyph(face, index, FT_LOAD_DEFAULT);
    if(error)
    {
        // error found
        return {0,0,false};
    }
    return {face->glyph->advance.x >> 6, face->glyph->advance.y >> 6, true};
}

void ft_font_t::put(int32_t chr, int level, int x, int y, frame_buffer_t & fb) const
{
	metrics_t metrics = get_metrics(chr);
    if(!metrics.exist) return; // non-existent character
    auto error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    if(error) return; // error exist on rendering glyph

	// adjust bounding box
    int pitch = face->glyph->bitmap.pitch;
	x += face->glyph->bitmap_left;
	y += GLYPH_HEIGHT_PX - 4 - face->glyph->bitmap_top; // TODO: fixme: 4 is a magic number depending on the font
	int fx = 0, fy = 0;
	int w = face->glyph->bitmap.width, h = face->glyph->bitmap.rows;

	// clip font bounding box
	if(!fb.clip(fx, fy, x, y, w, h)) return;

//printf("bitmap: %d %d %d %d %d\n", face->glyph->bitmap.pitch, face->glyph->bitmap_left, face->glyph->bitmap_top,
//    face->glyph->bitmap.rows, face->glyph->bitmap.width);
//printf("geometry: %d %d %d %d %d %d\n", fx, fy, x, y, w, h);

	// draw the pattern
	const unsigned char *p = face->glyph->bitmap.buffer;

	for(int yy = y; yy < h+y; ++yy, ++fy)
	{
		const uint8_t * line = p + fy * pitch;
		int fxx = fx;
		for(int xx = x; xx < w+x; ++xx, ++fxx)
		{
			int alpha = line[fxx];
			if(alpha)
			{
				int v = fb.get_point(xx, yy);
				v = alpha;
//				v = (((level - v) * alpha) >> 8 ) + v; // TODO: exact calculation
				fb.set_point(xx, yy, v);
			}
		}
	}

}



void init_font_ft()
{
    font_ft.begin();
}