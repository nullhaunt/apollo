#if VERTEX_SHADER
out gl_PerVertex
{
    vec4 gl_Position;
};

layout(location = 0) in vec4 modelPosition;
layout(location = 1) in vec2 modelUv;
layout(location = 0) out vec2 fragmentUv;

layout(std140) uniform Camera
{
    mat4 clipFromModel;
};

void main()
{
    gl_Position = clipFromModel * vec4(modelPosition.xyz, 1.0);
    fragmentUv = modelUv;
}
#endif

#if PIXEL_SHADER
layout(location = 0) in vec2 fragmentUv;
layout(location = 0) out vec4 outputColor;

layout(std140) uniform Material
{
    ivec4 mode;
    vec4 colors[3];
};

uniform sampler2D surfaceTexture;

void main()
{
    vec4 texel = vec4(1.0);
    if (mode.x != 0)
    {
        texel = texture(surfaceTexture, fragmentUv);
    }

    vec4 color;
    if (mode.x == 0)
    {
        color = vec4(colors[0].rgb, 1.0);
    }
    else if (mode.x == 1)
    {
        color = texel;
    }
    else if (mode.x == 2)
    {
        color = vec4(colors[0].rgb * texel.r + colors[1].rgb * texel.g + colors[2].rgb * texel.b, texel.a);
    }
    else if (mode.x == 3)
    {
        color = vec4(colors[0].rgb, texel.r);
    }
    else if (mode.x == 4)
    {
        color = vec4(colors[0].rgb * texel.r, 1.0);
    }
    else if (mode.x == 5)
    {
        color = vec4(colors[0].rgb * texel.r, pow(texel.g, colors[1].g));
    }
    else
    {
        color = vec4(1.0);
    }

    if (color.a <= 0.0)
    {
        discard;
    }

    outputColor = color;
}
#endif
