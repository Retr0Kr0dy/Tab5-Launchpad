#include "sgfx_text.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --- Minimal SGFXF v1 record layout (SDF or BITMAP A8) ------------------ */
typedef struct {
  uint32_t magic;      /* 'S','G','F','X' */
  uint16_t version;    /* 0x0001 */
  uint16_t kind;       /* 1=BITMAP_A8, 2=SDF_A8 */
  uint16_t atlas_w, atlas_h;
  int16_t  ascender, descender, line_gap;
  uint32_t glyph_count;
  uint32_t cmap_count;
  /* follows: glyph records then cmap records then atlas A8 payload */
} sgfxf_header_t;

typedef struct {
  uint32_t codepoint;   /* unicode */
  uint16_t gx, gy, gw, gh; /* atlas rect */
  int16_t  bearing_x, bearing_y;
  int16_t  advance;     /* in 26.6 units at design size 1.0 (we will scale) */
  float    norm_scale;  /* scale factor from 1.0 design to 1 px cap-height */
} sgfxf_glyph_t;

typedef struct {
  uint32_t codepoint;
  uint32_t glyph_index;
} sgfxf_cmap_t;

struct sgfx_font {
  sgfx_font_kind_t kind;
  int atlas_w, atlas_h;
  int ascender, descender, line_gap;
  uint32_t glyph_count;
  const sgfxf_glyph_t* glyphs;
  const sgfxf_cmap_t*  cmap;
  uint32_t cmap_count;
  const uint8_t* atlas_a8; /* pixels */
  int owns; /* whether we malloc'd the blob */
};

/* --- Builtin tiny SDF font (extern data blob) --------------------------- */
extern const unsigned char _sgfx_builtin_sdf[];
extern const size_t        _sgfx_builtin_sdf_len;

/* --- Loader ------------------------------------------------------------- */
static sgfx_font_t* font_from_blob(void* blob, size_t len, int take_ownership){
  if (len < sizeof(sgfxf_header_t)) return NULL;
  sgfxf_header_t* h = (sgfxf_header_t*)blob;
  if (h->magic != 0x58464753u /*'SGFX'*/ || h->version != 1) return NULL;
  sgfx_font_t* f = (sgfx_font_t*)calloc(1,sizeof(*f));
  f->kind = (sgfx_font_kind_t)h->kind;
  f->atlas_w = h->atlas_w; f->atlas_h = h->atlas_h;
  f->ascender = h->ascender; f->descender = h->descender; f->line_gap = h->line_gap;
  f->glyph_count = h->glyph_count;
  size_t off = sizeof(*h);
  f->glyphs = (const sgfxf_glyph_t*)((uint8_t*)blob + off);
  off += (size_t)h->glyph_count * sizeof(sgfxf_glyph_t);
  f->cmap = (const sgfxf_cmap_t*)((uint8_t*)blob + off);
  off += (size_t)h->cmap_count * sizeof(sgfxf_cmap_t);
  f->cmap_count = h->cmap_count;
  f->atlas_a8 = (const uint8_t*)((uint8_t*)blob + off);
  f->owns = take_ownership;
  return f;
}

sgfx_font_t* sgfx_font_open_builtin(void){
  return font_from_blob((void*)_sgfx_builtin_sdf, _sgfx_builtin_sdf_len, /*own=*/0);
}
void sgfx_font_close(sgfx_font_t* f){
  if(!f) return;
  if (f->owns){ free((void*)f->glyphs); } /* we owned whole blob */
  free(f);
}
sgfx_font_kind_t sgfx_font_kind(const sgfx_font_t* f){ return f?f->kind:0; }

sgfx_font_t* sgfx_font_load_from_memory(const void* data, size_t size){
  void* dup = malloc(size);
  if(!dup) return NULL;
  memcpy(dup,data,size);
  return font_from_blob(dup,size,/*own=*/1);
}

sgfx_font_t* sgfx_font_load_from_stream(sgfx_stream_read_fn r, sgfx_stream_seek_fn s, void* user){
  /* naive: read all to memory */
  if (!r) return NULL;
  enum { CH=4096 };
  size_t cap=CH, sz=0;
  uint8_t* buf=(uint8_t*)malloc(cap);
  if(!buf) return NULL;
  for(;;){
    if (sz+CH>cap){ cap*=2; uint8_t* nb=(uint8_t*)realloc(buf,cap); if(!nb){ free(buf); return NULL; } buf=nb; }
    size_t got = r(user, buf+sz, CH);
    sz += got;
    if (got < CH) break;
  }
  return font_from_blob(buf,sz,/*own=*/1);
}

/* --- CMap lookup (linear for tiny fonts; swap to binary if needed) ------ */
static const sgfxf_glyph_t* font_lookup(const sgfx_font_t* f, uint32_t cp){
  const sgfxf_cmap_t* cm=f->cmap;
  for(uint32_t i=0;i<f->cmap_count;++i) if(cm[i].codepoint==cp) return &f->glyphs[cm[i].glyph_index];
  return NULL;
}

/* --- UTF-8 next codepoint (ASCII fast path) ----------------------------- */
static const char* next_cp(const char* p, uint32_t* out){
  unsigned c = (unsigned char)*p++;
  if (c<0x80){ *out=c; return p; }
  /* minimal UTF-8 (2..4) */
  unsigned n=0; if ((c&0xE0)==0xC0){ n=1; c&=0x1F; }
  else if ((c&0xF0)==0xE0){ n=2; c&=0x0F; }
  else if ((c&0xF8)==0xF0){ n=3; c&=0x07; }
  while(n--){ unsigned cc=(unsigned char)*p++; c=(c<<6)|(cc&0x3F); }
  *out=c; return p;
}

/* --- Glyph cache (A8 at target px) -------------------------------------- */
typedef struct {
  uint32_t cp;
  int px;               /* rounded px size */
  int w,h, pitch;
  int bx, by;           /* bearing at target px */
  int adv;              /* advance at target px */
  uint8_t* a8;          /* owned */
  uint32_t lru;
} glyph_entry_t;

#ifndef SGFX_GLYPH_CACHE_N
#define SGFX_GLYPH_CACHE_N 64
#endif

typedef struct {
  glyph_entry_t slot[SGFX_GLYPH_CACHE_N];
  uint32_t tick;
} glyph_cache_t;

static glyph_cache_t G;

static void cache_init(void){ static int inited=0; if(!inited){ memset(&G,0,sizeof G); inited=1; } }
static glyph_entry_t* cache_find(uint32_t cp, int px){
  cache_init(); G.tick++;
  for (int i=0;i<SGFX_GLYPH_CACHE_N;++i){
    if (G.slot[i].cp==cp && G.slot[i].px==px){ G.slot[i].lru=G.tick; return &G.slot[i]; }
  }
  int k=0; for (int i=1;i<SGFX_GLYPH_CACHE_N;++i) if (G.slot[i].lru < G.slot[k].lru) k=i;
  if (G.slot[k].a8) { free(G.slot[k].a8); memset(&G.slot[k],0,sizeof(G.slot[k])); }
  return &G.slot[k];
}

/* --- SDF sampling utilities -------------------------------------------- */
static inline uint8_t clamp_u8(int v){ if(v<0) return 0; if(v>255) return 255; return (uint8_t)v; }
/* SDF is stored with 0..255 where 128 ≈ distance 0; scale factor chosen during bake */
static uint8_t sdf_sample(const uint8_t* img,int iw,int ih,int ix,int iy){
  if(ix<0) ix=0; if(iy<0) iy=0; if(ix>=iw) ix=iw-1; if(iy>=ih) iy=ih-1;
  return img[iy*iw+ix];
}

/* Rasterize a glyph from SDF to A8 at integer px size with AA + transforms */
static void rasterize_sdf(const sgfx_font_t* f, const sgfxf_glyph_t* g, int px,
                          float bold_px, float outline_px, float skew,
                          uint8_t** out_a8, int* ow,int* oh,int* opitch,
                          int* obx,int* oby,int* oadv)
{
  /* scale from font design to px: glyph metrics are at norm_scale per px */
  float S = (float)px * g->norm_scale;
  int gw = (int)ceilf(g->gw * S);
  int gh = (int)ceilf(g->gh * S);
  int pitch = gw;
  uint8_t* buf = (uint8_t*)malloc((size_t)gh*pitch); if(!buf){ *ow=*oh=*opitch=0; *out_a8=NULL; return; }
  memset(buf,0,(size_t)gh*pitch);

  /* inverse scale for sampling atlas */
  float invS = 1.0f / S;
  const uint8_t* atlas = f->atlas_a8;
  /* signed distances centered at 128; bold/outline shift thresholds */
  float bold_bias   = bold_px * 32.f;    /* tune vs your bake spread */
  float outline_rad = outline_px * 32.f;

  for(int y=0;y<gh;++y){
    float fy = ((float)y + 0.5f);
    for(int x=0;x<gw;++x){
      float fx = ((float)x + 0.5f);
      /* italic skew: sample from skewed x */
      float sx = fx + skew * (float)(y - gh);
      float u = (float)g->gx + (sx * invS);
      float v = (float)g->gy + (fy * invS);
      int iu = (int)floorf(u), iv = (int)floorf(v);
      /* simple bilinear */
      int u1 = iu+1, v1=iv+1;
      float fu = u - iu, fv = v - iv;
      uint8_t p00 = sdf_sample(atlas, f->atlas_w, f->atlas_h, iu,iv);
      uint8_t p10 = sdf_sample(atlas, f->atlas_w, f->atlas_h, u1,iv);
      uint8_t p01 = sdf_sample(atlas, f->atlas_w, f->atlas_h, iu,v1);
      uint8_t p11 = sdf_sample(atlas, f->atlas_w, f->atlas_h, u1,v1);
      float a0 = p00 + fu*(p10 - p00);
      float a1 = p01 + fu*(p11 - p01);
      float a  = a0 + fv*(a1 - a0);
      /* convert SDF to alpha (0..255): inside if value > 128 (+bias) */
      float dist = (a - 128.0f) - bold_bias;
      float alpha = 255.0f * fminf(fmaxf(0.5f + dist/32.0f, 0.0f), 1.0f); /* smoothstep-ish */
      buf[y*pitch + x] = clamp_u8((int)alpha);
    }
  }

  *out_a8 = buf; *ow=gw; *oh=gh; *opitch=pitch;
  *obx = (int)lrintf(g->bearing_x * S);
  *oby = (int)lrintf(g->bearing_y * S);
  *oadv= (int)lrintf(g->advance   * S);
}

/* --- Draw / Measure ------------------------------------------------------ */
static int round_px(float x){ return (int)lrintf(x); }

void sgfx_text_measure_line(const char* s, const sgfx_font_t* f,
                            const sgfx_text_style_t* st, sgfx_text_metrics_t* out)
{
  if(!s||!f||!st||!out) return;
  int px = round_px(st->px);
  int ascent  = (int)lrintf(f->ascender * st->px);
  int descent = (int)lrintf(-f->descender * st->px);
  int linegap = (int)lrintf((f->line_gap + st->line_gap_px));
  int adv=0, maxh=ascent+descent;
  for(const char* p=s; *p; ){
    uint32_t cp; p = next_cp(p,&cp);
    const sgfxf_glyph_t* g = font_lookup(f,cp);
    if(!g){ adv += px/2; continue; }
    glyph_entry_t* ge = cache_find(cp, px);
    if (!ge->a8){
      rasterize_sdf(f,g,px, st->bold_px, st->outline_px, st->italic_skew,
                    &ge->a8,&ge->w,&ge->h,&ge->pitch,&ge->bx,&ge->by,&ge->adv);
      ge->cp=cp; ge->px=px;
    }
    adv += ge->adv + (int)lrintf(st->letter_spacing);
    if (ge->h > maxh) maxh = ge->h;
  }
  out->ascent=ascent; out->descent=descent; out->line_gap=linegap;
  out->advance=adv; out->bbox_w=adv; out->bbox_h=maxh;
}

void sgfx_text_draw_line(sgfx_fb_t* fb, int x, int y,
                         const char* s, const sgfx_font_t* f,
                         const sgfx_text_style_t* st)
{
  if(!fb||!s||!f||!st) return;
  int px = round_px(st->px);
  int pen_x = x, baseline = y;
  /* optional shadow pass */
  if (st->shadow_alpha){
    sgfx_rgba8_t sc = st->color; sc.a = (uint8_t)((sc.a * (int)st->shadow_alpha)/255);
    for(const char* p=s; *p; ){
      uint32_t cp; p = next_cp(p,&cp);
      const sgfxf_glyph_t* g = font_lookup(f,cp); if(!g){ pen_x += px/2; continue; }
      glyph_entry_t* ge = cache_find(cp, px);
      if (!ge->a8){
        rasterize_sdf(f,g,px, st->bold_px, st->outline_px, st->italic_skew,
                      &ge->a8,&ge->w,&ge->h,&ge->pitch,&ge->bx,&ge->by,&ge->adv);
        ge->cp=cp; ge->px=px;
      }
      int gx = pen_x + ge->bx + st->shadow_dx;
      int gy = baseline - ge->by + st->shadow_dy;
      sgfx_fb_blit_a8(fb, gx, gy, ge->a8, ge->pitch, ge->w, ge->h, sc);
      pen_x += ge->adv + (int)lrintf(st->letter_spacing);
    }
    pen_x = x; /* reset for fill/outline pass */
  }

  /* outline first (if requested) */
  if (st->outline_px > 0.f && st->outline_alpha){
    sgfx_rgba8_t oc = st->outline_color; oc.a = st->outline_alpha;
    for(const char* p=s; *p; ){
      uint32_t cp; p = next_cp(p,&cp);
      const sgfxf_glyph_t* g = font_lookup(f,cp); if(!g){ pen_x += px/2; continue; }
      glyph_entry_t* ge = cache_find(cp, px);
      if (!ge->a8){
        rasterize_sdf(f,g,px, st->bold_px, st->outline_px, st->italic_skew,
                      &ge->a8,&ge->w,&ge->h,&ge->pitch,&ge->bx,&ge->by,&ge->adv);
        ge->cp=cp; ge->px=px;
      }
      int gx = pen_x + ge->bx;
      int gy = baseline - ge->by;
      /* crude outline by drawing a thickened version: small 8-neighborhood grow */
      for(int dy=-1; dy<=1; ++dy)
        for(int dx=-1; dx<=1; ++dx)
          sgfx_fb_blit_a8(fb, gx+dx, gy+dy, ge->a8, ge->pitch, ge->w, ge->h, oc);
      pen_x += ge->adv + (int)lrintf(st->letter_spacing);
    }
    pen_x = x;
  }

  /* fill */
  sgfx_rgba8_t fc = st->color; fc.a = (uint8_t)((fc.a * (int)st->fill_alpha)/255);
  for(const char* p=s; *p; ){
    uint32_t cp; p = next_cp(p,&cp);
    const sgfxf_glyph_t* g = font_lookup(f,cp); if(!g){ pen_x += px/2; continue; }
    glyph_entry_t* ge = cache_find(cp, px);
    if (!ge->a8){
      rasterize_sdf(f,g,px, st->bold_px, st->outline_px, st->italic_skew,
                    &ge->a8,&ge->w,&ge->h,&ge->pitch,&ge->bx,&ge->by,&ge->adv);
      ge->cp=cp; ge->px=px;
    }
    int gx = pen_x + ge->bx;
    int gy = baseline - ge->by;
    sgfx_fb_blit_a8(fb, gx, gy, ge->a8, ge->pitch, ge->w, ge->h, fc);
    pen_x += ge->adv + (int)lrintf(st->letter_spacing);
  }
}