uniform uint u_NodeID;
uniform sampler2DArray u_ChildrenSampler;

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

void triangleToSquare(inout vec2 P)
{
    if (P.x < P.y) {
        P.y+= P.x;
        P.x*= 2.0f;
    } else {
        P.x+= P.y;
        P.y*= 2.0f;
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
    int vertexID = gl_VertexID;

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

    vec2 uvLeft  = vec2(1.0f - u.x - u.y, u.y - u.x);
    vec2 uvRight = vec2(u.x - u.y, 1.0f - u.x - u.y);

    triangleToSquare(uvLeft);
    triangleToSquare(uvRight);

    vec4 textureLeft  = texture(u_ChildrenSampler, vec3(uvLeft, 0));
    vec4 textureRight = texture(u_ChildrenSampler, vec3(uvRight, 1));

    o_FragColor = vec4(uvLeft, 0, 0);
    o_FragColor = textureLeft + textureRight;
}
#endif
