#pragma sokol @ctype mat4 Mat4

#pragma sokol @vs vs
uniform vs_params {
    mat4 mvp;
};

in vec4 position;
in float palette_index0;
in vec2 uv0;

out float palette_index;
out vec2 uv;

void main() {
    gl_Position = mvp * position;
    palette_index = palette_index0;
    uv = uv0;
}
#pragma sokol @end

#pragma sokol @fs fs
uniform sampler2D palette;
uniform sampler2D tex;
in float palette_index;
in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = vec4(texture(tex, uv).r) * texture(
        palette,
        vec2(
            (0.5 + int(palette_index) % 8) / 8,
            (0.5 + int(palette_index / 8)) / 8
        )
    );
}
#pragma sokol @end

#pragma sokol @program triangle vs fs
