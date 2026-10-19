/* libSceFont on FreeType (Driveclub).
 *
 * The game opens its own TrueType/OpenType fonts from memory, sets a pixel scale and
 * rasterizes glyphs one at a time into an 8-bit render surface (its glyph cache), each at a
 * pen position on the baseline. FreeType rasterizes; this file maps the API onto it. Structure
 * layouts and placement follow BlackLineInteractive/Driveclub-Port's CoreText version
 * (GPL-2.0-or-later). The PS4 system font set has no files here: a Windows font stands in. */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "runtime.h"

#define FONT_ERROR_INVALID_PARAMETER ((int32_t)0x80460002)
#define FONT_ERROR_NO_SUPPORT_GLYPH ((int32_t)0x80460010)
#define FONT_ERROR_NO_SUPPORT_FONTSET ((int32_t)0x80460019)

typedef struct {
    float width, height;
    struct { float bearing_x, bearing_y, advance; } horizontal, vertical;
} GlyphMetrics;
_Static_assert(sizeof(GlyphMetrics) == 32, "SceFontGlyphMetrics");

typedef struct { float baseline_offset, line_advance, decoration_extent; } HorizontalLayout;
typedef struct { float offset_x, offset_y, position_x, position_y; } Kerning;

typedef struct {
    uint8_t *buffer;
    int32_t width_byte;
    int8_t pixel_size_byte;
    uint8_t pad0, style_flag, pad2;
    int32_t width, height;
    uint32_t sc_x0, sc_y0, sc_x1, sc_y1;
    uint64_t reserved[11];
} RenderSurface;
_Static_assert(sizeof(RenderSurface) == 128, "SceFontRenderSurface");

typedef struct {
    const void *stage;
    struct { uint8_t *address; uint32_t width_byte; uint8_t pixel_size_byte, pixel_format; uint16_t pad; } surface_image;
    struct { uint32_t x, y, w, h; } update_rect;
    struct { float bearing_x, bearing_y, advance, stride; uint32_t width, height; } image_metrics;
} RenderOutput;
_Static_assert(sizeof(RenderOutput) == 64, "SceFontRenderOutput");

typedef struct {
    FT_Face face;
    void *data;  /* FreeType reads the font from here for as long as the face lives */
    float scale_w, scale_h, sized_w, sized_h;
} Font;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static FT_Library library;
static _Alignas(64) uint8_t guest_library[256], guest_renderer[256];
static unsigned fonts_opened, glyphs_rendered;

static int ensure_library(void) {
    return library || !FT_Init_FreeType(&library);
}

static void apply_scale(Font *f) {
    if (f->sized_w == f->scale_w && f->sized_h == f->scale_h) return;
    FT_Set_Char_Size(f->face, (FT_F26Dot6)lroundf(f->scale_w * 64.0f), (FT_F26Dot6)lroundf(f->scale_h * 64.0f), 72, 72);
    f->sized_w = f->scale_w;
    f->sized_h = f->scale_h;
}

/* Loads `code` at the current scale with an optional fractional pen offset (pixels, y down). */
static int load_glyph(Font *f, uint32_t code, float fx, float fy, int render) {
    FT_UInt index = FT_Get_Char_Index(f->face, code);
    if (!index) return 0;
    apply_scale(f);
    FT_Vector delta = {(FT_Pos)lroundf(fx * 64.0f), (FT_Pos)lroundf(-fy * 64.0f)};
    FT_Set_Transform(f->face, NULL, render ? &delta : NULL);
    if (FT_Load_Glyph(f->face, index, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) return 0;
    if (render && FT_Render_Glyph(f->face->glyph, FT_RENDER_MODE_NORMAL)) return 0;
    return 1;
}

/* Metrics in pixels; y up from the baseline, as the API reports them. */
static void metrics(const Font *f, GlyphMetrics *m) {
    const FT_Glyph_Metrics *g = &f->face->glyph->metrics;
    memset(m, 0, sizeof(*m));
    m->width = g->width / 64.0f;
    m->height = g->height / 64.0f;
    m->horizontal.bearing_x = g->horiBearingX / 64.0f;
    m->horizontal.bearing_y = g->horiBearingY / 64.0f;
    m->horizontal.advance = g->horiAdvance / 64.0f;
    m->vertical.bearing_x = g->vertBearingX / 64.0f;
    m->vertical.bearing_y = g->vertBearingY / 64.0f;
    m->vertical.advance = g->vertAdvance / 64.0f;
}

static Font *open_font(const void *data, size_t size, const char *what) {
    if (!ensure_library()) return NULL;
    Font *f = calloc(1, sizeof(*f));
    if (!f || !(f->data = malloc(size))) { free(f); return NULL; }
    memcpy(f->data, data, size);
    if (FT_New_Memory_Face(library, f->data, (FT_Long)size, 0, &f->face)) {
        free(f->data);
        free(f);
        return NULL;
    }
    f->scale_w = f->scale_h = 16.0f;
    ++fonts_opened;
    printf("Font: opened %s %s (%zu bytes)\n", what, f->face->family_name ? f->face->family_name : "?", size);
    return f;
}

static ABI int32_t font_ok(void) { return 0; }

static ABI int32_t create_library(const void *mem, const void *params, uint64_t edition, void **out) {
    (void)mem, (void)params, (void)edition;
    if (!out) return FONT_ERROR_INVALID_PARAMETER;
    *out = guest_library;
    return 0;
}
static ABI int32_t create_renderer(const void *mem, const void *params, uint64_t edition, void **out) {
    (void)mem, (void)params, (void)edition;
    if (!out) return FONT_ERROR_INVALID_PARAMETER;
    *out = guest_renderer;
    return 0;
}
static ABI int32_t destroy_handle(void **handle) {
    if (handle) *handle = NULL;
    return 0;
}

static ABI int32_t open_font_memory(void *lib, const void *data, uint32_t size, const void *params, Font **out) {
    (void)lib, (void)params;
    if (!data || !size || !out) return FONT_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    Font *f = open_font(data, size, "game font");
    pthread_mutex_unlock(&lock);
    if (!f) {
        printf("Font: cannot open a %u-byte font\n", size);
        return FONT_ERROR_INVALID_PARAMETER;
    }
    *out = f;
    return 0;
}

/* System font sets: the closest Windows font. */
static ABI int32_t open_font_set(void *lib, uint32_t type, uint32_t mode, const void *params, Font **out) {
    (void)lib, (void)mode, (void)params;
    if (!out) return FONT_ERROR_INVALID_PARAMETER;
    static const char *const candidates[] = {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(*candidates); ++i) {
        FILE *file = fopen(candidates[i], "rb");
        if (!file) continue;
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        void *data = size > 0 ? malloc((size_t)size) : NULL;
        size_t read = data ? fread(data, 1, (size_t)size, file) : 0;
        fclose(file);
        if (read == (size_t)size && data) {
            pthread_mutex_lock(&lock);
            Font *f = open_font(data, read, "system font set");
            pthread_mutex_unlock(&lock);
            free(data);
            if (f) {
                printf("Font: system font set 0x%x -> %s\n", type, candidates[i]);
                *out = f;
                return 0;
            }
        } else {
            free(data);
        }
    }
    return FONT_ERROR_NO_SUPPORT_FONTSET;
}

static ABI int32_t close_font(Font *f) {
    pthread_mutex_lock(&lock);
    if (f) {
        FT_Done_Face(f->face);
        free(f->data);
        free(f);
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

static ABI int32_t set_scale_pixel(Font *f, float w, float h) {
    if (!f || w <= 0.0f || h <= 0.0f) return FONT_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    f->scale_w = w;
    f->scale_h = h;
    pthread_mutex_unlock(&lock);
    return 0;
}

static ABI int32_t horizontal_layout(Font *f, HorizontalLayout *layout) {
    if (!f || !layout) return FONT_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    apply_scale(f);
    const FT_Size_Metrics *s = &f->face->size->metrics;
    layout->baseline_offset = s->ascender / 64.0f;
    layout->line_advance = s->height / 64.0f;
    layout->decoration_extent = FT_MulFix(f->face->underline_thickness, s->y_scale) / 64.0f;
    pthread_mutex_unlock(&lock);
    return 0;
}

static ABI int32_t glyph_metrics(Font *f, uint32_t code, GlyphMetrics *m) {
    if (!f || !m) return FONT_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    int found = load_glyph(f, code, 0, 0, 0);
    if (found) metrics(f, m); else memset(m, 0, sizeof(*m));
    pthread_mutex_unlock(&lock);
    return found ? 0 : FONT_ERROR_NO_SUPPORT_GLYPH;
}

static ABI int32_t kerning(Font *f, uint32_t pre, uint32_t code, Kerning *k) {
    (void)f, (void)pre, (void)code;
    if (k) memset(k, 0, sizeof(*k));
    return 0;
}

static ABI void render_surface_init(RenderSurface *s, void *buffer, int32_t width_byte, int32_t pixel_size,
                                    int32_t width, int32_t height) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->buffer = buffer;
    s->width_byte = width_byte;
    s->pixel_size_byte = (int8_t)pixel_size;
    s->width = width > 0 ? width : 0;
    s->height = height > 0 ? height : 0;
    s->sc_x1 = (uint32_t)s->width;
    s->sc_y1 = (uint32_t)s->height;
}

static int32_t max_i(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t min_i(int32_t a, int32_t b) { return a < b ? a : b; }

/* Rasterizes `code` with its pen position at (x, y) (y down, on the baseline) into the
 * surface, as coverage. */
static ABI int32_t render_glyph(Font *f, uint32_t code, RenderSurface *s, float x, float y, GlyphMetrics *m,
                                RenderOutput *out) {
    if (!f || !s || !m || !out) return FONT_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&lock);
    const float fx = x - floorf(x), fy = y - floorf(y);
    if (!load_glyph(f, code, 0, 0, 0)) {
        memset(m, 0, sizeof(*m));
        memset(out, 0, sizeof(*out));
        pthread_mutex_unlock(&lock);
        return FONT_ERROR_NO_SUPPORT_GLYPH;
    }
    metrics(f, m);
    load_glyph(f, code, fx, fy, 1);
    const FT_GlyphSlot slot = f->face->glyph;
    const FT_Bitmap *bitmap = &slot->bitmap;
    const int32_t left = slot->bitmap_left, top = slot->bitmap_top;
    const int32_t gw = (int32_t)bitmap->width, gh = (int32_t)bitmap->rows;
    const int32_t right = left + gw;
    const int32_t dest_x = (int32_t)floorf(x) + left, dest_y = (int32_t)floorf(y) - top;
    const int32_t bpp = s->pixel_size_byte > 0 ? s->pixel_size_byte : 1;
    int32_t ux0 = dest_x, uy0 = dest_y, uw = 0, uh = 0;
    if (gw > 0 && gh > 0 && s->buffer && bitmap->buffer) {
        const int32_t x0 = max_i(dest_x, (int32_t)s->sc_x0), x1 = min_i(dest_x + gw, (int32_t)s->sc_x1);
        const int32_t y0 = max_i(dest_y, (int32_t)s->sc_y0), y1 = min_i(dest_y + gh, (int32_t)s->sc_y1);
        if (x1 > x0 && y1 > y0) {
            for (int32_t row = y0; row < y1; ++row) {
                const uint8_t *src = bitmap->buffer + (ptrdiff_t)(row - dest_y) * bitmap->pitch;
                uint8_t *dst = s->buffer + (size_t)row * (size_t)s->width_byte;
                for (int32_t col = x0; col < x1; ++col)
                    memset(dst + (size_t)col * (size_t)bpp, src[col - dest_x], (size_t)bpp);
            }
            ux0 = x0, uy0 = y0, uw = x1 - x0, uh = y1 - y0;
        }
    }
    ++glyphs_rendered;
    memset(out, 0, sizeof(*out));
    out->update_rect.x = (uint32_t)max_i(ux0, 0);
    out->update_rect.y = (uint32_t)max_i(uy0, 0);
    out->update_rect.w = (uint32_t)uw;
    out->update_rect.h = (uint32_t)uh;
    out->surface_image.address = s->buffer ? s->buffer + (size_t)out->update_rect.y * (size_t)s->width_byte +
                                             (size_t)out->update_rect.x * (size_t)bpp : NULL;
    out->surface_image.width_byte = (uint32_t)s->width_byte;
    out->surface_image.pixel_size_byte = (uint8_t)bpp;
    const float advance_end = ceilf(x + m->horizontal.advance);
    out->image_metrics.bearing_x = (float)((int32_t)floorf(x) + left) - x;
    out->image_metrics.bearing_y = (float)top + floorf(y) - y;
    out->image_metrics.advance = advance_end - x;
    out->image_metrics.stride = fmaxf(advance_end, floorf(x) + (float)right) - x;
    out->image_metrics.width = (uint32_t)gw;
    out->image_metrics.height = (uint32_t)gh;
    pthread_mutex_unlock(&lock);
    return 0;
}

static const RuntimeExport exports[] = {
    {"sceFontMemoryInit", font_ok}, {"sceFontCreateLibraryWithEdition", create_library},
    {"sceFontCreateRendererWithEdition", create_renderer}, {"sceFontDestroyLibrary", destroy_handle},
    {"sceFontDestroyRenderer", destroy_handle}, {"sceFontAttachDeviceCacheBuffer", font_ok},
    {"sceFontSupportExternalFonts", font_ok}, {"sceFontSupportSystemFonts", font_ok},
    {"sceFontSelectLibraryFt", font_ok}, {"sceFontSelectRendererFt", font_ok},
    {"sceFontOpenFontMemory", open_font_memory}, {"sceFontOpenFontSet", open_font_set},
    {"sceFontCloseFont", close_font}, {"sceFontBindRenderer", font_ok}, {"sceFontUnbindRenderer", font_ok},
    {"sceFontSetScalePixel", set_scale_pixel}, {"sceFontSetupRenderScalePixel", set_scale_pixel},
    {"sceFontGetHorizontalLayout", horizontal_layout}, {"sceFontGetCharGlyphMetrics", glyph_metrics},
    {"sceFontGetKerning", kerning}, {"sceFontRenderSurfaceInit", render_surface_init},
    {"sceFontRenderCharGlyphImage", render_glyph}, {"sceFontRenderCharGlyphImageHorizontal", render_glyph},
};

uintptr_t runtime_font_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
