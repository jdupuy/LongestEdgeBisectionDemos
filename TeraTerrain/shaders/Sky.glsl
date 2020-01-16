uniform mat4 u_Projection;
uniform mat4 u_InvView;
uniform vec3 u_SunDir;
uniform float u_FarPlane;

#ifdef VERTEX_SHADER
layout(location = 0) out vec3 o_ViewDir;
void main()
{
    vec2 ndcPos = 2.0f * vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1) - 1.0f;
    vec3 viewSpacePos = vec3(ndcPos * 1e5, -u_FarPlane / 2.0f);

    o_ViewDir = vec3(u_InvView * vec4(normalize(viewSpacePos), 0));
    gl_Position = u_Projection * vec4(viewSpacePos, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec3 i_ViewDir;
layout(location = 0) out vec4 o_FragColor;

void main()
{
#if 1
    vec3 worldSunDir = u_SunDir.zxy;
    vec3 worldCamera = u_InvView[3].zxy;
    vec3 v = normalize(i_ViewDir).zxy;

    vec3 sunColor = step(cos(3.1415926f / 180.0f), dot(v, worldSunDir))
                  * vec3(SUN_INTENSITY);
    vec3 extinction;
    vec3 inscatter = skyRadiance(worldCamera + earthPos, v, worldSunDir, extinction);
    vec3 finalColor = sunColor * extinction + inscatter;
#endif
    o_FragColor.rgb = finalColor;
    o_FragColor.a = 1.0;
}
#endif
