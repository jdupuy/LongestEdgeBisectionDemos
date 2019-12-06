uniform sampler2D u_ChunkDmapSampler;
uniform sampler2D u_ChunkAmapSampler;
uniform sampler2D u_ChunkNmapSampler;

#ifdef VERTEX_SHADER
layout(location = 0) out vec2 o_TexCoord;

void main()
{
    o_TexCoord = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    gl_Position = vec4(2.0 * o_TexCoord - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main()
{
    // reconstruct the normal map
    vec2 Nc = 2.0 * texture(u_ChunkNmapSampler, i_TexCoord).xy - 1.0;
    vec3 N = vec3(Nc, sqrt(1.0 - dot(Nc, Nc)));
    o_FragColor = vec4(abs(N), 1);
}
#endif
