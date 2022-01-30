#include <math.h>

#define SOKOL_IMPL
#if defined(_MSC_VER)
    #define SOKOL_D3D11
    #define SOKOL_LOG(str) OutputDebugStringA(str)
#elif defined(__EMSCRIPTEN__)
    #define SOKOL_GLES2
#elif defined(__APPLE__)
    // NOTE: on macOS, sokol.c is compiled explicitly as ObjC 
    #define SOKOL_METAL
#else
    #define SOKOL_GLCORE33
#endif

#include "sokol/sokol_time.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"

static float signum(float f) { return (f > 0) - (f < 0); }
static float lerp_rads(float a, float b, float t) {
  float difference = fmodf(b - a, M_PI*2.0f),
        distance = fmodf(2.0f * difference, M_PI*2.0f) - difference;
  return a + distance * t;
}

#define vec2(_x, _y) ((Vec2) { .x = (_x), .y = (_y) })
typedef union { struct { float x, y; }; float arr[2]; } Vec2;
static float vec2_rads(Vec2 a) { return atan2f(a.y, a.x); }
static Vec2 sub2(Vec2 a, Vec2 b) { return vec2(a.x-b.x, a.y-b.y); }
static Vec2 add2(Vec2 a, Vec2 b) { return vec2(a.x+b.x, a.y+b.y); }
// static Vec2 sub2f(Vec2 a, float f) { return vec2(a.x-f, a.y-f); }
static Vec2 div2f(Vec2 a, float f) { return vec2(a.x/f, a.y/f); }
static Vec2 mul2f(Vec2 a, float f) { return vec2(a.x*f, a.y*f); }
static float dot2(Vec2 a, Vec2 b) { return a.x*b.x + a.y*b.y; }
static float mag2(Vec2 a) { return sqrtf(dot2(a, a)); }
static Vec2 lerp2(Vec2 a, Vec2 b, float t) { return add2(mul2f(a, 1.0f - t), mul2f(b, t)); }
static float dist2(Vec2 a, Vec2 b) { return mag2(sub2(a, b)); }
static Vec2 norm2(Vec2 a) { return div2f(a, mag2(a) ?: 1.0f); }
static Vec2 refl2(Vec2 d, Vec2 norm) { return sub2(d, mul2f(norm, 2.0f*dot2(d, norm))); }

typedef struct { float arr[2][2]; } Mat2;
static Mat2 z_rot2x2(float rads) {
    float sin = sinf(rads), cos = cosf(rads);
    return (Mat2) {
        .arr = {
            {  cos,  sin },
            { -sin,  cos },
        }
    };
}
static Vec2 mul2x22(Mat2 m, Vec2 v) {
  return vec2(
      m.arr[0][0] * v.arr[0] + m.arr[1][0] * v.arr[1],
      m.arr[0][1] * v.arr[0] + m.arr[1][1] * v.arr[1]
  );
}

typedef struct { float arr[4][4]; } Mat4;
static Mat4 ortho4x4(float left, float right, float bottom, float top, float near, float far) {
    Mat4 res = {0};

    res.arr[0][0] = 2.0f / (right - left);
    res.arr[1][1] = 2.0f / (top - bottom);
    res.arr[2][2] = 2.0f / (near - far);
    res.arr[3][3] = 1.0f;

    res.arr[3][0] = (left + right) / (left - right);
    res.arr[3][1] = (bottom + top) / (bottom - top);
    res.arr[3][2] = (far + near) / (near - far);

    return res;
}

typedef struct { float arr[4]; } Vec4;
static Vec4 mul4x44(Mat4 m, Vec4 v) {
  Vec4 res;
  for(int x = 0; x < 4; ++x) {
    float sum = 0;
    for(int y = 0; y < 4; ++y)
      sum += m.arr[y][x] * v.arr[y];

    res.arr[x] = sum;
  }
  return res;
}


#include "build/shaders.glsl.h"

#include "build/map.h"

// #define STB_RECT_PACK_IMPLEMENTATION
// #include "stb/stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

typedef uint64_t Tick;

typedef struct { float x, y, z, color, u, v; } Vert;
typedef struct {
    Vert *verts;
    uint16_t *idxs;
    int nvert, nidx;
    float min_z, max_z;
    sg_bindings bind;
} Geo;
static Geo geo_alloc(int nvert, int nidx) {
    return (Geo) {
        .nvert = nvert,
        .nidx = nidx,
        .verts = calloc(sizeof(Vert), nvert),
        .idxs = calloc(sizeof(uint16_t), nidx),
    };
}
static void geo_bind_init(Geo *geo, const char *lvert, const char *lidx, sg_usage usg) {
    geo->bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc) {
        .size = sizeof(Vert) * geo->nvert,
        .data = (usg == SG_USAGE_IMMUTABLE)
            ? (sg_range) { .ptr = geo->verts, .size = geo->nvert * sizeof(Vert) }
            : (sg_range) { 0 },
        .usage = usg,
        .label = lvert
    });
    geo->bind.index_buffer = sg_make_buffer(&(sg_buffer_desc) {
        .size = sizeof(uint16_t) * geo->nidx,
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .data = (usg == SG_USAGE_IMMUTABLE)
            ? (sg_range) { .ptr = geo->idxs, .size = geo->nidx * sizeof(uint16_t) }
            : (sg_range) { 0 },
        .usage = usg,
        .label = lidx
    });
}
float geo_min_z = -1.0f, geo_max_z = 1.0f;
static void geo_find_z_range(Vert *beg, Vert *end) {
    for (Vert *i = beg; i != end; i++)
        geo_min_z = (i->z < geo_min_z) ? i->z : geo_min_z,
        geo_max_z = (i->z > geo_max_z) ? i->z : geo_max_z;
}

typedef enum {
    EntMask_Player  = (1 << 0),
    EntMask_Terrain = (1 << 1),
} EntMask;

#define SWING_DURATION 50
typedef struct Ent Ent;
struct Ent {
    uint8_t active;
    EntMask mask;
    float radius;
    Vec2 pos, vel;
    struct { Tick end; Vec2 toward; } swing;
};

/* application state */
static struct {
    MapData map;
    
    uint8_t keys[SAPP_MAX_KEYCODES];

    Ent ents[1 << 10];

    uint64_t frame; /* a sokol_time tick, not one of our game ticks */
    Tick tick;
    double fixed_tick_accumulator;

    Ent *player;
    Vec2 cam;

    Geo static_geo, dyn_geo;
    size_t static_geo_n_idx;

    sg_pipeline pip;
    sg_pass_action pass_action;
} state;
#define ENT_MAX (sizeof(state.ents) / sizeof(state.ents[0]))

#define SYSTEM(e) for (Ent *e = state.ents; (e - state.ents) < ENT_MAX; e++) if (e->active)
Ent *ent_alloc(void) {
    for (int i = 0; i < ENT_MAX; i++)
        if (!state.ents[i].active) {
            state.ents[i] = (Ent) { .active = true, .swing.toward.x = 1.0f };
            return state.ents + i;
        }
    puts("entity pool exhausted"), exit(1);
}
void ent_free(Ent *ent) { ent->active = false; }

stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs

#define PALETTE \
    X(Color_White,        1.00f, 1.00f, 1.00f, 1.00f) \
    X(Color_Brown,        0.50f, 0.42f, 0.31f, 1.00f) \
    X(Color_DarkBrown,    0.30f, 0.25f, 0.18f, 1.00f) \
    X(Color_Blue,         0.00f, 0.47f, 0.95f, 1.00f) \
    X(Color_Red,          0.90f, 0.16f, 0.22f, 1.00f) \
    X(Color_Green,        0.00f, 0.89f, 0.19f, 1.00f) \
    X(Color_Grey,         0.51f, 0.51f, 0.51f, 1.00f) \
    X(Color_DarkGrey,     0.31f, 0.31f, 0.31f, 1.00f) \
    X(Color_Yellow,       0.99f, 0.98f, 0.00f, 1.00f) \
    X(Color_ForestShadow, 0.18f, 0.43f, 0.36f, 1.00f) \
    X(Color_TreeBorder,   0.00f, 0.42f, 0.13f, 1.00f) \
    X(Color_TreeGreen,    0.00f, 0.46f, 0.17f, 1.00f) \
    X(Color_TreeGreen1,   0.04f, 0.50f, 0.21f, 1.00f) \
    X(Color_TreeGreen2,   0.08f, 0.54f, 0.25f, 1.00f) \
    X(Color_TreeGreen3,   0.12f, 0.58f, 0.29f, 1.00f) \

#define X(name, r, g, b, a) name,
typedef enum { PALETTE } Color;
#undef X

#define GAME_SCALE (11.8f)
static Mat4 mvp4x4(void) {
    float f_range = 1.0f / (geo_max_z - geo_min_z);

    float xx = 1.0f / GAME_SCALE;
    float yy = sapp_widthf() / sapp_heightf() / GAME_SCALE;
    float zz = f_range;
    float zw = -f_range * geo_min_z;
    Mat4 res = {
        .arr = {
            {   xx, 0.0f, 0.0f, 0.0f },
            { 0.0f,   yy, 0.0f, 0.0f },
            { 0.0f, 0.0f,   zz, 0.0f },
            { 0.0f, 0.0f,   zw, 1.0f },
        }
    };

    Vec4 trans_cam = mul4x44(res, (Vec4) {{
        -state.cam.x, -state.cam.y, 0.0f, 1.0f
    }});

    res.arr[3][0] = trans_cam.arr[0];
    res.arr[3][1] = trans_cam.arr[1];
    return res;
}


typedef struct {
    Geo *geo;
    uint16_t *idx;
    Vert *vert;
} GeoWtr;
static GeoWtr geo_wtr(Geo *geo) {
    return (GeoWtr) { .geo = geo, .vert = geo->verts, .idx = geo->idxs };
}

static void geo_wtr_flush(GeoWtr *wtr) {
    size_t v_used = wtr->vert - wtr->geo->verts;
    uint32_t max_v = wtr->geo->nvert;
    if (v_used > max_v) printf("%ld/%u verts used!\n", v_used, max_v), exit(1);

    size_t i_used = wtr->idx - wtr->geo->idxs;
    uint32_t max_i = wtr->geo->nidx;
    if (i_used > max_i) printf("%ld/%u idxs used!\n", i_used, max_i), exit(1);


    sg_update_buffer(wtr->geo->bind.vertex_buffers[0], &(sg_range) {
        .ptr = wtr->geo->verts,
        .size = (wtr->vert - wtr->geo->verts) * sizeof(Vert),
    });

    sg_update_buffer(wtr->geo->bind.index_buffer, &(sg_range) {
        .ptr = wtr->geo->idxs,
        .size = (wtr->idx - wtr->geo->idxs) * sizeof(uint16_t),
    });
}

static void write_circ(GeoWtr *wtr, float x, float y, float r, Color clr, float z) {
    size_t start = wtr->vert - wtr->geo->verts;
    uint16_t circ_indices[] = {
        2 + start, 6 + start, 8 + start,
        0 + start, 1 + start, 8 + start,
        1 + start, 2 + start, 8 + start,
        2 + start, 3 + start, 4 + start,
        4 + start, 5 + start, 2 + start,
        5 + start, 6 + start, 2 + start,
        6 + start, 7 + start, 8 + start
    };
    memcpy(wtr->idx, circ_indices, sizeof(circ_indices));
    wtr->idx += sizeof(circ_indices) / sizeof(uint16_t);

    *(wtr->vert)++ = (Vert) { x + r *  0.0000f, y + r *  1.0000f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r * -0.6428f, y + r *  0.7660f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r * -0.9848f, y + r *  0.1736f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r * -0.8660f, y + r * -0.5000f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r * -0.3420f, y + r * -0.9397f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r *  0.3420f, y + r * -0.9397f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r *  0.8660f, y + r * -0.5000f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r *  0.9848f, y + r *  0.1736f, z, clr };
    *(wtr->vert)++ = (Vert) { x + r *  0.6428f, y + r *  0.7660f, z, clr };
}


static void write_tri(GeoWtr *wtr, Vert v0, Vert v1, Vert v2) {
    size_t start = wtr->vert - wtr->geo->verts;
    *(wtr->vert)++ = v0;
    *(wtr->vert)++ = v1;
    *(wtr->vert)++ = v2;

    *(wtr->idx)++ = start + 0;
    *(wtr->idx)++ = start + 1;
    *(wtr->idx)++ = start + 2;
}

static void write_quad(GeoWtr *wtr, Vert v0, Vert v1, Vert v2, Vert v3) {
    size_t start = wtr->vert - wtr->geo->verts;
    *(wtr->vert)++ = v0;
    *(wtr->vert)++ = v1;
    *(wtr->vert)++ = v2;
    *(wtr->vert)++ = v3;

    *(wtr->idx)++ = start + 0;
    *(wtr->idx)++ = start + 1;
    *(wtr->idx)++ = start + 2;
    *(wtr->idx)++ = start + 0;
    *(wtr->idx)++ = start + 2;
    *(wtr->idx)++ = start + 3;
}

static void write_rect(GeoWtr *wtr, float x, float y, float w, float h, Color clr, float z) {
    write_quad(
        wtr,
        (Vert) { x - w/2.0f, y    , z, clr },
        (Vert) { x + w/2.0f, y    , z, clr },
        (Vert) { x + w/2.0f, y + h, z, clr },
        (Vert) { x - w/2.0f, y + h, z, clr }
    );
}

#define GOLDEN_RATIO (1.618034f)
static void write_sword(GeoWtr *wtr, float rads, float x, float y, float z) {
    Vert *start = wtr->vert;
    write_tri(wtr,
        (Vert) {  0.075f,         0.0f, z, Color_DarkBrown },
        (Vert) { -0.075f,         0.0f, z, Color_DarkBrown },
        (Vert) { -0.075f, GOLDEN_RATIO, z, Color_DarkBrown }
    );
    write_tri(wtr,
        (Vert) {  0.0f, GOLDEN_RATIO, z, Color_Grey },
        (Vert) {  0.1f,        0.35f, z, Color_Grey },
        (Vert) {  0.2f,        1.35f, z, Color_Grey }
    );
    write_tri(wtr,
        (Vert) {  0.0f, GOLDEN_RATIO, z, Color_Grey },
        (Vert) { -0.1f,        0.35f, z, Color_Grey },
        (Vert) { -0.2f,        1.35f, z, Color_Grey }
    );
    write_tri(wtr,
        (Vert) { -0.1f,        0.35f, z, Color_Grey },
        (Vert) {  0.1f,        0.35f, z, Color_Grey },
        (Vert) {  0.0f, GOLDEN_RATIO, z, Color_Grey }
    );
    float t = 0.135f, w = 0.225f;
    write_rect(wtr, 0.0f, 0.4f - t, 2.0f * w, t, Color_DarkGrey, z);

    Mat2 m = z_rot2x2(rads);
    for (Vert *i = start; i < wtr->vert; i++) {
        Vec2 p = mul2x22(m, vec2(i->x, i->y));
        i->x = x + p.x;
        i->y = y + p.y;
    }
}

#define map (state.map)
static size_t write_map(Geo *geo) {
    GeoWtr wtr = geo_wtr(geo); 

    for (MapData_Tree *t = map.trees; (t - map.trees) < map.ntrees; t++) {
        float w = 0.8f, h = GOLDEN_RATIO, r = 0.4f;
        write_circ(&wtr, t->x, t->y + r, r, Color_Brown, t->y);
        write_rect(&wtr, t->x, t->y + r, w, h, Color_Brown, t->y);

        write_circ(&wtr, t->x + 0.80f, t->y + 2.2f, 0.8f, Color_TreeGreen,  t->y - 1.1f);
        write_circ(&wtr, t->x + 0.16f, t->y + 3.0f, 1.0f, Color_TreeGreen1, t->y - 1.1f);
        write_circ(&wtr, t->x - 0.80f, t->y + 2.5f, 0.9f, Color_TreeGreen2, t->y - 1.1f);
        write_circ(&wtr, t->x - 0.16f, t->y + 2.0f, 0.8f, Color_TreeGreen3, t->y - 1.1f);

        write_circ(&wtr, t->x + 0.80f, t->y + 2.2f, 0.8f+0.1f, Color_TreeBorder, t->y);
        write_circ(&wtr, t->x + 0.16f, t->y + 3.0f, 1.0f+0.1f, Color_TreeBorder, t->y);
        write_circ(&wtr, t->x - 0.80f, t->y + 2.5f, 0.9f+0.1f, Color_TreeBorder, t->y);
        write_circ(&wtr, t->x - 0.16f, t->y + 2.0f, 0.8f+0.1f, Color_TreeBorder, t->y);

        float sr = 0.82f;
        write_circ(&wtr, t->x, t->y + r, sr, Color_ForestShadow, t->y + sr);
    }

    geo_find_z_range(geo->verts, wtr.vert);
    return wtr.idx - geo->idxs;
}
#undef map

static void init(void) {
    state.player = ent_alloc();
    state.player->mask = EntMask_Player;

    stm_setup();
    sg_setup(&(sg_desc){ .context = sapp_sgcontext() });

    state.map = parse_map_data(fopen("build/map.bytes", "rb"));
    state.static_geo = geo_alloc(1 << 16, 1 << 18);
    state.static_geo_n_idx = write_map(&state.static_geo);
    geo_bind_init(&state.static_geo, "static_vert", "static_idx", SG_USAGE_IMMUTABLE);
    free(state.static_geo.verts);
    free(state.static_geo.idxs);

#define map (state.map)
    for (MapData_Circle *t = map.circles; (t - map.circles) < map.ncircles; t++) {
        Ent *circ = ent_alloc();
        circ->mask = EntMask_Terrain;
        circ->radius = t->radius;
        circ->pos = vec2(t->x, t->y);
    }
#undef map

    state.dyn_geo = geo_alloc(1 << 15, 1 << 17);
    geo_bind_init(&state.dyn_geo, "dyn_vert", "dyn_idx", SG_USAGE_STREAM);

    uint8_t palette[4*4*4] = {0}, *plt_wtr = palette;

#define X(name, r, g, b, a) \
    *plt_wtr++ = (uint8_t)(r * 255.0); \
    *plt_wtr++ = (uint8_t)(g * 255.0); \
    *plt_wtr++ = (uint8_t)(b * 255.0); \
    *plt_wtr++ = (uint8_t)(a * 255.0); 
    PALETTE
#undef X
    
    /* NOTE: tex_slot is provided by shader code generation */
    state.static_geo.bind.fs_images[SLOT_palette] =
    state.dyn_geo.bind.fs_images[SLOT_palette] = sg_make_image(&(sg_image_desc){
        .width = 4,
        .height = 4,
        .data.subimage[0][0] = SG_RANGE(palette),
        .label = "palette-texture"
    });

    /* -- font tex init -- */
    unsigned char ttf_buffer[1<<20];
    unsigned char temp_bitmap[512*512];
    if (!fread(ttf_buffer, 1, 1<<20, fopen("./WackClubSans-Regular.ttf", "rb")))
        perror("couldn't get font");
    // no guarantee this fits!
    stbtt_BakeFontBitmap(ttf_buffer,0, 20.0, temp_bitmap,512,512, 32,96, cdata);
    temp_bitmap[0] = 255;
    state.dyn_geo.bind.fs_images[SLOT_tex] =
    state.static_geo.bind.fs_images[SLOT_tex] = sg_make_image(&(sg_image_desc){
        .width = 512,
        .height = 512,
        .pixel_format = SG_PIXELFORMAT_R8,
        .data.subimage[0][0] = SG_RANGE(temp_bitmap),
        .label = "font-texture"
    });

    /* create shader from code-generated sg_shader_desc */
    sg_shader shd = sg_make_shader(triangle_shader_desc(sg_query_backend()));

    /* create a pipeline object (default render states are fine for triangle) */
    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [ATTR_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
                [ATTR_vs_palette_index0].format = SG_VERTEXFORMAT_FLOAT,
                [ATTR_vs_uv0].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .colors[0].blend = (sg_blend_state) {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_ONE, 
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
            .src_factor_alpha = SG_BLENDFACTOR_ONE, 
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true
        },
        .label = "default-pipeline"
    });

    /* a pass action to framebuffer to black */
    state.pass_action = (sg_pass_action) {
        .colors[0] = { .action=SG_ACTION_CLEAR, .value={ 0.255f, 0.51f, 0.439f, 1.0f } }
    };
}

static void event(const sapp_event *ev) {
    switch (ev->type) {
    case SAPP_EVENTTYPE_KEY_UP:
    case SAPP_EVENTTYPE_KEY_DOWN: {
        state.keys[ev->key_code] = ev->type == SAPP_EVENTTYPE_KEY_DOWN;
        if (ev->key_code == SAPP_KEYCODE_ESCAPE)
            sapp_request_quit();
    } break;
    case SAPP_EVENTTYPE_MOUSE_DOWN: {
        Vec2 cam = state.cam;
        float ar = sapp_widthf() / sapp_heightf(); /* aspect ratio */
        float x = -(1.0f - ev->mouse_x / sapp_widthf()  * 2.0f) * GAME_SCALE        + cam.x;
        float y =  (1.0f - ev->mouse_y / sapp_heightf() * 2.0f) * (GAME_SCALE / ar) + cam.y;
        if (state.tick > state.player->swing.end) {
            state.player->swing.end = state.tick + SWING_DURATION;
            state.player->swing.toward = norm2(sub2(vec2(x, y), state.player->pos));
        }
    } break;
    default: {}
    }
}

unsigned int positive_inf = 0x7F800000; // 0xFF << 23
#define POS_INF_F (*(float *)&positive_inf)
static float scene_distance(Vec2 p, EntMask mask, Ent **ent) {
    float dist = POS_INF_F;

    SYSTEM(e) {
        if (e->mask & mask) continue;
        float this_dist = dist2(p, e->pos) - e->radius;
        if (this_dist < dist) {
            dist = this_dist;
            if (ent) *ent = e;
        }
    }
    return dist;
}

static float raymarch(Vec2 origin, Vec2 dir, EntMask mask, Ent **hit) {
    float t = 0.0f;
    for (int iter = 0; iter < 5; iter++) {
        float d = scene_distance(add2(origin, mul2f(dir, t)), mask, hit);
        if (d == POS_INF_F) return d;
        if (d < 0.01f) return t;
        t += d;
    }
    return t;
}

#define TICK_MS (1000.0f / 60.0f)
static void tick(void) {
    state.tick++;

    state.cam = lerp2(state.cam, add2(state.player->pos, vec2(0.0f, 0.5f)), 0.05f);

    Vec2 move = {0};
    if (state.keys[SAPP_KEYCODE_W]) move.y += 1.0;
    if (state.keys[SAPP_KEYCODE_S]) move.y -= 1.0;
    if (state.keys[SAPP_KEYCODE_A]) move.x -= 1.0;
    if (state.keys[SAPP_KEYCODE_D]) move.x += 1.0;
    float speed = (
        state.player->vel.x == 0.0f ||
            signum(state.player->vel.x) == signum(state.player->swing.toward.x)
        ) ? 0.0075f : 0.0045f;
    state.player->vel = add2(state.player->vel, mul2f(norm2(move), speed));

    SYSTEM(e) {
        float vel_mag = mag2(e->vel);
        if (vel_mag <= 0.0f) continue;

        float d;
        Ent *closest_ent;
        float closest_dist = raymarch(e->pos, e->vel, e->mask, &closest_ent) - e->radius;
        if (closest_dist <= 0.0f) {
            e->vel = mul2f(refl2(norm2(e->vel), norm2(sub2(e->pos, closest_ent->pos))), vel_mag);
            d = vel_mag;
        } else {
            d = fminf(vel_mag, closest_dist);
        }

        // e->pos = add2(e->pos, mul2f(e->vel, d / vel_mag));
        e->pos = add2(e->pos, mul2f(norm2(e->vel), d));
        e->vel = mul2f(e->vel, 0.93f);
    }
}

static void frame(void) {
    double elapsed = stm_ms(stm_laptime(&state.frame));
    state.fixed_tick_accumulator += elapsed;
    while (state.fixed_tick_accumulator > TICK_MS) {
        state.fixed_tick_accumulator -= TICK_MS;
        tick();
    }

    GeoWtr wtr = geo_wtr(&state.dyn_geo); 

    { /* push game ents */
        Ent *e = state.player;
        Vec2 ppos = e->pos;
        write_rect(&wtr, ppos.x, ppos.y, 1.0f, 1.0f, Color_Blue, ppos.y);

        Vec2 center = vec2(0.0f, 0.5f);
        Vec2 hand_pos = add2(center, mul2f(e->swing.toward, 0.5f));
        float rot = vec2_rads(e->swing.toward) - M_PI_2;
        float dir = signum(e->swing.toward.x);

        float rest_rot;
        Vec2 rest_pos;
        {
            float vl = mag2(state.player->vel);
            float tickf = state.tick;
            float drag = fminf(vl, 0.07f);
            float breathe = sinf(tickf / 35.0f) / 30.0f;
            float jog = sinf(tickf / 6.85f) * fminf(vl, 0.175f);
            rest_rot = (M_PI_2 + breathe + jog) * -dir;
            rest_pos.x = (0.55 + breathe / 3.2 + jog * 1.5 + drag) * -dir;
            rest_pos.y = 0.35 + (breathe + jog) / 2.8 + drag * 0.5;
        }

        typedef enum {
            KF_Rotates = (1 << 1),
            KF_Moves   = (1 << 2),
            KF_Damages = (1 << 3),
        } KF_Flag;
        typedef struct { float duration; KF_Flag flags; float rot; Vec2 pos; } KF;

        float swing = 0.5f * dir;
        KF frames[] = {
            { 0.2174f,              KF_Rotates | KF_Moves, rot - swing * 1.0, hand_pos },
            { 0.2304f,              KF_Rotates           , rot - swing * 2.0           },
            { 0.0870f, KF_Damages | KF_Rotates           , rot + swing * 2.0           },
            { 0.2478f,              KF_Rotates           , rot + swing * 3.0           },
            { 0.2174f,              KF_Rotates | KF_Moves,          rest_rot, rest_pos },
        };

        float sword_rot = rest_rot;
        Vec2 sword_pos = rest_pos;

        float time = (e->swing.end - state.tick) / ((float) SWING_DURATION);
        if (time > 0.0f) {
            for (KF *f = frames; (f - frames) < sizeof(frames) / sizeof(frames[0]); f++) {

                if (time > f->duration) {
                    time -= f->duration;
                    if (f->flags & KF_Rotates) sword_rot = f->rot;
                    if (f->flags & KF_Moves) sword_pos = f->pos;
                    continue;
                };

                float t = time / f->duration;
                if (f->flags & KF_Rotates) sword_rot = lerp_rads(sword_rot, f->rot, t);
                if (f->flags & KF_Moves) sword_pos = lerp2(sword_pos, f->pos, t);
                break;
            }
        }

        sword_pos.x += ppos.x;
        sword_pos.y += ppos.y;
        write_sword(&wtr, sword_rot, sword_pos.x, sword_pos.y, sword_pos.y - 1.0f);

        geo_find_z_range(state.dyn_geo.verts, wtr.vert);
    }

    uint16_t *text_start = wtr.idx;
    { /* push text */
        float x = sapp_widthf() - 80.0f, y = sapp_heightf();
        char buf[10];
        sprintf(buf, "%d FPS", (int)roundf(1.0f / sapp_frame_duration()));
        for (char *text = buf; *text; text++) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
            write_quad(
                &wtr,
                (Vert) { q.x0, q.y0, 1.0f, Color_White, q.s0, q.t1 },
                (Vert) { q.x1, q.y0, 1.0f, Color_White, q.s1, q.t1 },
                (Vert) { q.x1, q.y1, 1.0f, Color_White, q.s1, q.t0 },
                (Vert) { q.x0, q.y1, 1.0f, Color_White, q.s0, q.t0 }
            );
        }
    }

    geo_wtr_flush(&wtr);

    sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(state.pip);

    vs_params_t vs_params = { .mvp = mvp4x4() };
    sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_vs_params, &SG_RANGE(vs_params));

    sg_apply_bindings(&state.static_geo.bind);
    sg_draw(0, state.static_geo_n_idx, 1);

    sg_apply_bindings(&state.dyn_geo.bind);
    sg_draw(0, text_start - state.dyn_geo.idxs, 1);


    sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_vs_params, &SG_RANGE(((vs_params_t) {
        .mvp = ortho4x4(0.0f, sapp_widthf(), 0.0f, sapp_heightf(), -1.0f, 1.0f),
    })));
    sg_draw(text_start - state.dyn_geo.idxs, wtr.idx - text_start, 1);

    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
        .width = 640,
        .height = 480,
        .gl_force_gles2 = true,
        .window_title = "rpgc",
        .icon.sokol_default = true,
        .sample_count = 4,
    };
}
