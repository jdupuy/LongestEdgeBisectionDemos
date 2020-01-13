uniform uint u_NodeID;
uniform sampler2DArray u_NodeSampler;

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

vec2 SplitLeft(vec2 u)
{
    return vec2(1.0f - u.x - u.y, u.y - u.x);
}

vec2 SplitRight(vec2 u)
{
    return vec2(u.x - u.y, 1.0f - u.x - u.y);
}

// express texCoords in left neighbor
vec2 Left(vec2 u)
{
    return vec2(u.y, -u.x);
}

// express texCoords in right neighbor
vec2 Right(vec2 u)
{
    return vec2(-u.y, u.x);
}

// express texCoords in edge neighbor
vec2 Edge(vec2 u)
{
    return 1.0f - u;
}

void main()
{
    vec2 u = i_TexCoord;
    squareToTriangle(u);

    // compute tex coords
    vec2 uBaseLeft  = SplitLeft(u);
    vec2 uBaseRight = SplitRight(u);
    vec2 uEdgeLeft  = SplitLeft(Edge(u));
    vec2 uEdgeRight = SplitRight(Edge(u));
    vec2 uRightLeft = SplitLeft(Right(u));
    vec2 uLeftRight = SplitRight(Left(u));

    triangleToSquare(uBaseLeft);
    triangleToSquare(uBaseRight);
    triangleToSquare(uEdgeLeft);
    triangleToSquare(uEdgeRight);
    triangleToSquare(uRightLeft);
    triangleToSquare(uLeftRight);

    // fetch data
    vec4 textureBaseLeft  = texture(u_NodeSampler, vec3(uBaseLeft, 0));
    vec4 textureBaseRight = texture(u_NodeSampler, vec3(uBaseRight, 1));
    vec4 textureEdgeLeft  = texture(u_NodeSampler, vec3(uEdgeLeft, 2));
    vec4 textureEdgeRight = texture(u_NodeSampler, vec3(uEdgeRight, 3));
    vec4 textureRightLeft = 0.0f * texture(u_NodeSampler, vec3(uRightLeft, 4));
    vec4 textureLeftRight = 0.0f * texture(u_NodeSampler, vec3(uLeftRight, 5));
    // fetch nrm
    float nrm = texture(u_NodeSampler, vec3(uBaseLeft, 7)).r
              + texture(u_NodeSampler, vec3(uBaseRight, 7)).r
              + texture(u_NodeSampler, vec3(uEdgeLeft, 7)).r
              + texture(u_NodeSampler, vec3(uEdgeRight, 7)).r
              + 0.0f * texture(u_NodeSampler, vec3(uRightLeft, 7)).r
              + 0.0f * texture(u_NodeSampler, vec3(uLeftRight, 7)).r;

    // export data
    o_FragColor = textureBaseLeft
                + textureBaseRight
                + textureEdgeLeft
                + textureEdgeRight
                + textureRightLeft
                + textureLeftRight;
    o_FragColor/= nrm;
}
#endif
