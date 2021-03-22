#include <stdlib.h>
#include "font_ft.h"
#include <esp_partition.h>
#include "mz_update.h"
#include "frame_buffer.h"
#include "freetype/internal/ftdebug.h"
#include "lru_cache/lru_cache.hpp"

static FT_Library library; // the FT library instance
static constexpr auto FT_LOAD_FLAGS = FT_LOAD_DEFAULT;
static constexpr auto FT_RENDER_FLAGS = FT_RENDER_MODE_NORMAL;

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


// a class for metrics cache
class metrics_cache_t
{
    static constexpr size_t CACHE_SIZE = 1024u;

    FT_Face face;
#pragma pack(push, 1)
    // cache entry item
    struct entry_t
    {
        uint8_t exist; // existence
        int8_t adv_x; // step x
        int8_t adv_y; // step y
        int8_t left; // bitmap left
        int8_t top; // bitmap top
        uint8_t w; // bitmap width
        uint8_t h; // bitmap height
    };
#pragma pack(pop)

    entry_t fn(const uint32_t & chr)
    {
        auto index = FT_Get_Char_Index(face, chr);
        if(!index)
        {
            // undefined character code
            return {0,0,0,0,0,0,0}; // non existent
        }
        auto error = FT_Load_Glyph(face, index, FT_LOAD_FLAGS);
        if(error)
        {
            // error found
            return {0,0,0,0,0,0,0}; // non existent
        }
        return {
            true, // exist
            (typeof entry_t::adv_x) (face->glyph->advance.x >> 6),
            (typeof entry_t::adv_y) (face->glyph->advance.y >> 6),
            (typeof entry_t::left)  (face->glyph->bitmap_left),
            (typeof entry_t::top)   (face->glyph->bitmap_top),
            (typeof entry_t::w)     (face->glyph->bitmap.width),
            (typeof entry_t::h)     (face->glyph->bitmap.rows)
            };
    }

    lru_cache_using_std_unordered_map<uint32_t, entry_t> lru;

public:
    metrics_cache_t() : face(nullptr),
        lru(std::bind(&metrics_cache_t::fn, this, std::placeholders::_1), CACHE_SIZE)
    {;}

    void set_face(FT_Face face)  { this->face = face; }

    entry_t get_metrics(int32_t chr)
    {
        return lru(chr);
    }
};



ft_font_t::ft_font_t() : face(nullptr), cache(new metrics_cache_t)
{
}


void ft_font_t::begin()
{
    unsigned long fre = xPortGetFreeHeapSize();
    printf("Memory free area before FreeType font load: %ld\n", fre);
//    FT_Trace_Enable();
//    setenv("FT2_DEBUG", "any:7", 1);
    _begin();
//    unsetenv("FT2_DEBUG");
//    FT_Trace_Disable();
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

    cache->set_face(face);
}

ft_font_t::~ft_font_t() // will not called
{
    FT_Done_Face(face); // will not called
}


ft_font_t::metrics_t ft_font_t::get_metrics(int32_t chr) const
{
    auto metrics = cache->get_metrics(chr);
    return {metrics.adv_x, metrics.adv_y, metrics.exist};
}


void ft_font_t::put(int32_t chr, int level, int x, int y, frame_buffer_t & fb) const
{
	auto metrics = cache->get_metrics(chr);
    if(!metrics.exist) return; // non-existent character

	// adjust bounding box
	x += metrics.left;
	y += GLYPH_HEIGHT_PX - 3 - metrics.top; // TODO: fixme: 3 is a magic number depending on the font
	int fx = 0, fy = 0;
	int w = metrics.w,
        h = metrics.h;

	// clip font bounding box
	if(!fb.clip(fx, fy, x, y, w, h)) return;

    // at this point, the character which is completely out of screen, will not
    // comes here.

    // some drawing positions remaining; render
    auto index = FT_Get_Char_Index(face, chr);
    if(!index)
    {
        // undefined character code
        // TODO: panic
        return;
    }
    auto error = FT_Load_Glyph(face, index, FT_LOAD_FLAGS);
    if(error)
    {
        // error found
        return;
    }
   
    error = FT_Render_Glyph(face->glyph, FT_RENDER_FLAGS);
    if(error) return; // error exist on rendering glyph

    int pitch = face->glyph->bitmap.pitch;

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