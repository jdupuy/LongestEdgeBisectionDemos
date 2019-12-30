uniform uint u_NodeID;
uniform sampler2D u_InputSampler;

//layout(location = 0, rgba8) uniform image2D u_OutputSampler;
layout(location = 0, rgba32f) uniform image2D u_OutputSampler;

void squareToTriangle(inout vec2 p)
{
    if (p.y > p.x) {
        p.x*= 0.5f;
        p.y-= p.x;
    } else {
        p.y*= 0.5f;
        p.x-= p.y;
    }
}

vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

#ifdef VERTEX_SHADER
layout(location = 0) out vec2 o_TexCoord;

void main()
{
    int vertexID = gl_VertexID % 4;

    o_TexCoord = vec2(vertexID & 1, vertexID >> 1 & 1);
    gl_Position = vec4(o_TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
}
#endif // VERTEX_SHADER

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;

layout(location = 0) out vec4 o_FragColor;

void main()
{
    vec2 u = i_TexCoord;
    squareToTriangle(u);

    leb_Node node = leb_Node(u_NodeID, findMSB(u_NodeID));
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
    vec2 p1 = vec2(pos[0][0], pos[1][0]);
    vec2 p2 = vec2(pos[0][1], pos[1][1]);
    vec2 p3 = vec2(pos[0][2], pos[1][2]);
    vec2 uv = BarycentricInterpolation(vec2[3](p1, p2, p3), u);
    vec4 data = texture(u_InputSampler, uv);

    //data = vec4(u, 0, 1);//texture(u_InputSampler, u);
    if (u_NodeID > 0u) {
        imageStore(u_OutputSampler, ivec2(gl_FragCoord.xy), data);
    } else {
        imageStore(u_OutputSampler, ivec2(gl_FragCoord.xy), vec4(1));
    }

    o_FragColor = vec4(data);
}
#endif
