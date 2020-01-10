
uniform mat4 u_ModelViewProjection;

#ifdef VERTEX_SHADER
layout (location = 0) out vec2 o_TexCoord;

void main()
{
    vec2 P = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1);

    o_TexCoord = P;
    gl_Position = u_ModelViewProjection * vec4(P, 0.0f, 1.0f);
}
#endif

#ifdef FRAGMENT_SHADER
layout (location = 0) in vec2 i_TexCoord;
layout (location = 0) out vec4 o_FragColor;

void main()
{
    TT_Texel t = TT_TextureFetch(i_TexCoord);
    vec3 Li = normalize(vec3(1, 1, 1));

    vec3 normal = normalize(vec3(-t.slope, 1));
    o_FragColor = vec4(t.slope, 0, 1);

    float roughness = TT__TerrainRoughness(i_TexCoord, 40.0f);
    o_FragColor = vec4(vec3(roughness), 1);
    o_FragColor = vec4(vec3(mod(t.altitude, 0.5f)) * 2.0f, 1);
    o_FragColor = vec4(normal, 1);
    o_FragColor = vec4(t.albedo * clamp(dot(normal, Li), 0.0f, 1.0f), 1);
}
#endif