uniform uint u_NodeID;
uniform sampler2D u_InputSampler;

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

vec3 SlopeToNormal(vec2 slope)
{
    return normalize(vec3(-slope, 1));
}

vec2 NormalToDisk(vec3 normal)
{
    return normal.xy;
}

vec2 DiskToSquare(vec2 disk)
{
    float pi = 3.14159265359f;
    float r = sqrt(dot(disk, disk));
    float phi = atan(disk.y, disk.x);
    float a, b;

    if (phi < -pi / 4.0f) {
        phi += 2.0f * pi;
    }

    if (phi < pi / 4.0f) {
        a = r;
        b = phi * a / (pi / 4.0f);
    } else if (phi < 3.0f * pi / 4.0f) {
        b = r;
        a = -(phi - pi / 2.0f) * b / (pi / 4.0f);
    } else if (phi < 5.0f * pi / 4.0f) {
        a = -r;
        b = (phi - pi) * a / (pi / 4.0f);
    } else {
        b = -r;
        a = -(phi - 3.0f * pi / 2.0f) * b / (pi / 4.0f);
    }

    return 0.5f * (1.0f + vec2(a, b));
}

vec2 SlopeToSquare(vec2 slope)
{
    return DiskToSquare(NormalToDisk(SlopeToNormal(slope)));
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

layout(location = 0) out vec4   o_Albedo;
layout(location = 1) out float  o_Displacement;
layout(location = 2) out vec2   o_Normal;

void main()
{
    if (u_NodeID > 0) {
        vec2 u = i_TexCoord;
        squareToTriangle(u);

        leb_Node node = leb_Node(u_NodeID, findMSB(u_NodeID));
        vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
        mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
        vec2 p1 = vec2(pos[0][0], pos[1][0]);
        vec2 p2 = vec2(pos[0][1], pos[1][1]);
        vec2 p3 = vec2(pos[0][2], pos[1][2]);
        vec2 uv = BarycentricInterpolation(vec2[3](p1, p2, p3), u);
        TT_Texel texel = TT_TextureFetch(uv);

        float displacementScale = 32.0f;
        o_Albedo = vec4(texel.albedo, 1.0);
        o_Displacement = (texel.altitude + 14.0f / displacementScale) / (1600.0f / displacementScale + 14.0f / displacementScale);
        o_Normal = SlopeToSquare(texel.slope);
    } else /* NULL node */{
        o_Albedo = vec4(1.0);
        o_Displacement = 1.0f;
        o_Normal = vec2(1.0f);
    }
}
#endif
