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
#include "sokol/util/sokol_color.h"

#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb/stb_truetype.h"

typedef struct { float x, y, z, color, u, v; } Vert;
typedef struct {
    Vert *verts;
    int vert_bytes;
    uint16_t *idxs;
    int idx_bytes;
} Geo;
static Geo malloc_geo(int vert_n, int idx_n) {
    return (Geo) {
        .verts = calloc(sizeof(Vert), vert_n),
        .vert_bytes =   sizeof(Vert) * vert_n,
        .idxs = calloc(sizeof(uint16_t), idx_n),
        .idx_bytes =   sizeof(uint16_t) * idx_n,
    };
}

/* application state */
static struct {
    Geo dyn_geo;
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
} state;

stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs

static void init(void) {
    state.dyn_geo = malloc_geo(1 << 10, 1 << 11);

    sg_setup(&(sg_desc){
        .context = sapp_sgcontext()
    });

    state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = state.dyn_geo.vert_bytes,
        .usage = SG_USAGE_STREAM,
        .label = "stream-vertices"
    });

    state.bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
        .size = state.dyn_geo.idx_bytes,
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .usage = SG_USAGE_STREAM,
        .label = "static-indices"
    });

    uint8_t palette[4*4*4] = {0};
    int i = 0;
    sg_color color;
#define WRITE_COLOR(input)                     \
    color = (sg_color) input;                  \
    palette[i++] = (uint8_t)(color.r * 255.0); \
    palette[i++] = (uint8_t)(color.g * 255.0); \
    palette[i++] = (uint8_t)(color.b * 255.0); \
    palette[i++] = (uint8_t)(color.a * 255.0); \

    WRITE_COLOR(SG_WHITE);
    WRITE_COLOR(SG_BLUE);
    WRITE_COLOR(SG_RED);
    WRITE_COLOR(SG_GREEN);
    WRITE_COLOR(SG_YELLOW);
#undef WRITE_COLOR
    
    /* NOTE: tex_slot is provided by shader code generation */
    state.bind.fs_images[SLOT_palette] = sg_make_image(&(sg_image_desc){
        .width = 4,
        .height = 4,
        .data.subimage[0][0] = SG_RANGE(palette),
        .label = "palette-texture"
    });

    /* -- font tex init -- */
    unsigned char ttf_buffer[1<<20];
    unsigned char temp_bitmap[512*512];
    if (!fread(ttf_buffer, 1, 1<<20, fopen("./Ubuntu-R.ttf", "rb")))
        perror("couldn't get font");
    // no guarantee this fits!
    stbtt_BakeFontBitmap(ttf_buffer,0, 32.0, temp_bitmap,512,512, 32,96, cdata);
    state.bind.fs_images[SLOT_tex] = sg_make_image(&(sg_image_desc){
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
        /* if the vertex layout doesn't have gaps, don't need to provide strides and offsets */
        .layout = {
            .attrs = {
                [ATTR_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
                [ATTR_vs_palette_index0].format = SG_VERTEXFORMAT_FLOAT,
                [ATTR_vs_uv0].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .label = "default-pipeline"
    });

    /* a pass action to framebuffer to black */
    state.pass_action = (sg_pass_action) {
        .colors[0] = { .action=SG_ACTION_CLEAR, .value={0.0f, 0.0f, 0.0f, 1.0f } }
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

static void push_quad_idxs(Geo *geo, int quad_n) {
    geo->idxs[quad_n * 6 + 0] = quad_n * 4 + 0;
    geo->idxs[quad_n * 6 + 1] = quad_n * 4 + 1;
    geo->idxs[quad_n * 6 + 2] = quad_n * 4 + 2;
    geo->idxs[quad_n * 6 + 3] = quad_n * 4 + 0;
    geo->idxs[quad_n * 6 + 4] = quad_n * 4 + 2;
    geo->idxs[quad_n * 6 + 5] = quad_n * 4 + 3;
}

static void frame(void) {
    int quad_n = 0;

    Vert vertices[] = {
        // positions            // colors
        { -0.5f, 0.0f, 0.5f,     2.0f, },
        {  0.5f, 0.0f, 0.5f,     2.0f, },
        {  0.5f, 1.0f, 0.5f,     2.0f, },
        { -0.5f, 1.0f, 0.5f,     2.0f, },
    };
    push_quad_idxs(&state.dyn_geo, quad_n++);
    for (int i = 0; i < sizeof(vertices) / sizeof(Vert); i++)
        vertices[i].x *=                           1.0f / 11.8f,
        vertices[i].y *= sapp_widthf() / sapp_heightf() / 11.8f;
    memcpy(state.dyn_geo.verts, vertices, sizeof(vertices));

    // Vert *v_wtr = state.dyn_geo.verts;
    // float x = 0.0f, y = 20.0f;
    // for (char *text = "hello world"; *++text;) {
    //     stbtt_aligned_quad q;
    //     stbtt_GetBakedQuad(cdata, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
    //     *v_wtr++ = (Vert) { q.x0, q.y0, 0.0f, 0.0f, q.s0, q.t0 };
    //     *v_wtr++ = (Vert) { q.x1, q.y0, 0.0f, 0.0f, q.s1, q.t0 };
    //     *v_wtr++ = (Vert) { q.x1, q.y1, 0.0f, 0.0f, q.s1, q.t1 };
    //     *v_wtr++ = (Vert) { q.x0, q.y1, 0.0f, 0.0f, q.s0, q.t1 };
    //     push_quad_idxs(&state.dyn_geo, quad_n++);
    // }
    // for (int i = 0; i < v_wtr - state.dyn_geo.verts; i++)
    //     state.dyn_geo.verts[i].x = 2.0f * state.dyn_geo.verts[i].x / sapp_widthf() - 1.0,
    //     state.dyn_geo.verts[i].y = 1.0 - 2.0f * state.dyn_geo.verts[i].y / sapp_heightf();

    sg_update_buffer(state.bind.vertex_buffers[0], &(sg_range) {
        .ptr = state.dyn_geo.verts,
        .size = state.dyn_geo.vert_bytes,
    });

    sg_update_buffer(state.bind.index_buffer, &(sg_range) {
        .ptr = state.dyn_geo.idxs,
        .size = state.dyn_geo.idx_bytes,
    });

    sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);
    sg_draw(0, quad_n * 6, 1);
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
        .window_title = "Triangle (sokol-app)",
        .icon.sokol_default = true,
        .sample_count = 4,
    };
}
