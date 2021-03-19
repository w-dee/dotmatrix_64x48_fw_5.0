#pragma once

#include "fonts/font.h"
#include <ft2build.h>
#include FT_FREETYPE_H

class frame_buffer_t;
class metrics_cache_t;

class ft_font_t : public font_base_t
{
    static constexpr int GLYPH_HEIGHT_PX = 15; // pixel height. At this point this is fixed value.

    FT_Face face;
    metrics_cache_t *cache;
public:
    ft_font_t();
    ~ft_font_t();

    void begin();

	virtual metrics_t get_metrics(int32_t chr) const;

	virtual void put(int32_t chr, int level, int x, int y, frame_buffer_t & fb) const;

	bool get_available() const { return face != nullptr; }

	virtual int get_height() const { return GLYPH_HEIGHT_PX; }

private:
   void _begin();

};

extern ft_font_t font_ft;

void init_font_ft();

