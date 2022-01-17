#pragma sokol @vs vs
in vec4 position;
in float palette_index0;

out float palette_index;

void main() {
    gl_Position = position;
    palette_index = palette_index0;
}
#pragma sokol @end

#pragma sokol @fs fs
uniform sampler2D palette;
in float palette_index;
out vec4 frag_color;

void main() {
    frag_color = texture(
        palette,
        vec2(
            (0.5 + int(palette_index) % 4) / 4,
            (0.5 + int(palette_index / 4)) / 4
        )
    );
}
#pragma sokol @end

#pragma sokol @program triangle vs fs
