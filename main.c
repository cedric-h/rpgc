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

#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "build/shaders.glsl.h"
#include "build/map.h"

// #define STB_RECT_PACK_IMPLEMENTATION
// #include "stb/stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

typedef struct { float x, y, z, color, u, v; } Vert;
typedef struct {
    Vert *verts;
    uint16_t *idxs;
    int nvert, nidx;
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

/* application state */
static struct {
    MapData map;
    Geo static_geo, dyn_geo;
    size_t static_geo_n_idx;
    sg_pipeline pip;
    sg_pass_action pass_action;
} state;

stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs


#define PALETTE \
    X(Color_White,      1.00f, 1.00f, 1.00f, 1.00f) \
    X(Color_Brown,      0.50f, 0.42f, 0.31f, 1.00f) \
    X(Color_Blue,       0.00f, 0.47f, 0.95f, 1.00f) \
    X(Color_Red,        0.90f, 0.16f, 0.22f, 1.00f) \
    X(Color_Green,      0.00f, 0.89f, 0.19f, 1.00f) \
    X(Color_Yellow,     0.99f, 0.98f, 0.00f, 1.00f) \
    X(Color_DarkGreen,  0.00f, 0.46f, 0.17f, 1.00f) \
    X(Color_DarkGreen1, 0.04f, 0.50f, 0.21f, 1.00f) \
    X(Color_DarkGreen2, 0.08f, 0.54f, 0.25f, 1.00f) \
    X(Color_DarkGreen3, 0.12f, 0.58f, 0.29f, 1.00f) \

#define X(name, r, g, b, a) name,
typedef enum { PALETTE } Color;
#undef X

typedef struct {
    Geo *geo;
    uint16_t *idx;
    Vert *vert;
} GeoWtr;
static GeoWtr geo_wtr(Geo *geo) {
    return (GeoWtr) { .geo = geo, .vert = geo->verts, .idx = geo->idxs };
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

static void game_project_verts(Vert *beg, Vert *end) {
    float min_z = -1.0f, max_z = 1.0f;
    for (Vert *i = beg; i != end; i++)
        min_z = (i->z < min_z) ? i->z : min_z,
        max_z = (i->z > max_z) ? i->z : max_z;

    for (Vert *i = beg; i != end; i++)
        i->x *=                           1.0f / 11.8f,
        i->y *= sapp_widthf() / sapp_heightf() / 11.8f,
        i->z = 1.0f - 2.0f * (i->z - min_z) / (max_z - min_z);
}

#define map (state.map)
static size_t write_map(Geo *geo) {
    GeoWtr wtr = geo_wtr(geo); 

    for (MapData_Tree *t = map.trees; (t - map.trees) < map.ntrees; t++) {
        float w = 0.8f, h = 1.618034f, r = 0.4f;
        write_circ(&wtr, t->x, t->y + r, r, Color_Brown, -t->y);
        write_rect(&wtr, t->x, t->y + r, w, h, Color_Brown, -t->y);
        write_circ(&wtr, t->x + 0.80f, t->y + 2.2f, 0.8f, Color_DarkGreen, -t->y);
        write_circ(&wtr, t->x + 0.16f, t->y + 3.0f, 1.0f, Color_DarkGreen1, -t->y);
        write_circ(&wtr, t->x - 0.80f, t->y + 2.5f, 0.9f, Color_DarkGreen2, -t->y);
        write_circ(&wtr, t->x - 0.16f, t->y + 2.0f, 0.8f, Color_DarkGreen3, -t->y);
    }

    game_project_verts(geo->verts, wtr.vert);
    return wtr.idx - geo->idxs;
}
#undef map

static void init(void) {
    sg_setup(&(sg_desc){
        .context = sapp_sgcontext()
    });

    state.map = parse_map_data(fopen("build/map.bytes", "rb"));
    state.static_geo = geo_alloc(1 << 15, 1 << 17);
    state.static_geo_n_idx = write_map(&state.static_geo);
    geo_bind_init(&state.static_geo, "static_vert", "static_idx", SG_USAGE_IMMUTABLE);

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
    state.dyn_geo.bind.fs_images[SLOT_tex] = state.static_geo.bind.fs_images[SLOT_tex] = sg_make_image(&(sg_image_desc){
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
        case SAPP_EVENTTYPE_KEY_DOWN: {
            if (ev->key_code == SAPP_KEYCODE_ESCAPE)
                sapp_request_quit();
        } break;
        default: {}
    }
}


static void frame(void) {
    GeoWtr wtr = geo_wtr(&state.dyn_geo); 

    { /* push game ents */
        write_rect(&wtr, 0.0f, 0.0f, 1.0f, 1.0f, Color_Blue, 0.0f);

        game_project_verts(state.dyn_geo.verts, wtr.vert);
    }

    { /* push text */
        Vert *text_start = wtr.vert;
        float x = 20.0f, y = 20.0f;
        for (char *text = "hello world"; *text; text++) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
            write_quad(
                &wtr,
                (Vert) { q.x0, q.y0, -1.0f, Color_White, q.s0, q.t0 },
                (Vert) { q.x1, q.y0, -1.0f, Color_White, q.s1, q.t0 },
                (Vert) { q.x1, q.y1, -1.0f, Color_White, q.s1, q.t1 },
                (Vert) { q.x0, q.y1, -1.0f, Color_White, q.s0, q.t1 }
            );
        }
        for (Vert *i = text_start; i != wtr.vert; i++)
            i->x = 2.0f * i->x / sapp_widthf() - 1.0f,
            i->y = 1.0f - 2.0f * i->y / sapp_heightf();
    }

    size_t v_used = wtr.vert - state.dyn_geo.verts;
    uint32_t max_v = state.dyn_geo.nvert;
    if (v_used > max_v) printf("%ld/%u verts used!\n", v_used, max_v), exit(1);

    size_t i_used = wtr.idx - state.dyn_geo.idxs;
    uint32_t max_i = state.dyn_geo.nidx;
    if (i_used > max_i) printf("%ld/%u idxs used!\n", i_used, max_i), exit(1);


    sg_update_buffer(state.dyn_geo.bind.vertex_buffers[0], &(sg_range) {
        .ptr = state.dyn_geo.verts,
        .size = (wtr.vert - state.dyn_geo.verts) * sizeof(Vert),
    });

    sg_update_buffer(state.dyn_geo.bind.index_buffer, &(sg_range) {
        .ptr = state.dyn_geo.idxs,
        .size = (wtr.idx - state.dyn_geo.idxs) * sizeof(uint16_t),
    });

    sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(state.pip);

    sg_apply_bindings(&state.static_geo.bind);
    sg_draw(0, state.static_geo_n_idx, 1);

    sg_apply_bindings(&state.dyn_geo.bind);
    sg_draw(0, wtr.idx - state.dyn_geo.idxs, 1);

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
