uniform sampler2D u_ChunkDmapSampler;
uniform sampler2D u_ChunkAmapSampler;
uniform sampler2D u_ChunkNmapSampler;

float fbm(vec2 P, float smoothness, int octaves) {
    float value = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value+= SimplexPerlin2D(P) * exp2(-smoothness * i);
        P*= 2.0f;
    }

    return value;
}

float sediment(vec2 P)
{
    float x = P.y + fbm(P, 0.25, 16) / 32.0f;
    float arg = x * 6.28318530718f - 1.57079632679f;
    return smoothstep(0.70f, 0.85f, sin(arg) * 0.5f + 0.5f);
}

vec2 ConcentricMapFwd(vec2 u)
{
    const float PI = 3.14159265359f;
    float r1 = 2.0f * u.x - 1.0f;
    float r2 = 2.0f * u.y - 1.0f;
    float phi, r;

    if (r1 == 0.0f && r2 == 0.0f) {
        r = phi = 0.0f;
    } else if (r1 * r1 > r2 * r2) {
        r = r1;
        phi = (PI / 4.0f) * (r2 / r1);
    } else {
        r = r2;
        phi = (PI / 2.0f) - (r1 / r2) * (PI / 4);
    }

    return r * vec2(cos(phi), sin(phi));
}

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
    float z = texture(u_ChunkDmapSampler, i_TexCoord).x;
    vec3 albedo = texture(u_ChunkAmapSampler, i_TexCoord).xyz;
    // reconstruct the normal map
    vec2 Np = texture(u_ChunkNmapSampler, i_TexCoord).xy;
    vec2 Nd = ConcentricMapFwd(Np);
    vec3 N = vec3(Nd, sqrt(max(0.0, 1.0 - dot(Nd, Nd))));
    vec3 wi = normalize(vec3(1,1,1));

    vec2 P = i_TexCoord;
    o_FragColor = vec4(albedo * max(dot(wi, N), 0.0), 1);
    o_FragColor = vec4(albedo, 1);
    o_FragColor = vec4(vec3(z), 1);
    o_FragColor = vec4(vec3(fbm(i_TexCoord, 0.5, 16)) * 0.5 + 0.5, 1);
    o_FragColor = vec4(vec3(sediment(P * 1.0)), 1);
    o_FragColor = vec4(albedo * (N.z * 0.5 + 0.5), 1);
    o_FragColor = vec4(abs(N * 0.5 + 0.5), 1);
}
#endif
