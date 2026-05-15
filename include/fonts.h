#pragma once
#include "types.h"

#define GLYPH_NUM 128

extern const uint8_t g_vnl_fonts[GLYPH_NUM][8];

int glyph_index(char ch);
