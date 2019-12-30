#line 1

#ifndef TT_LEB_ID
#   define TT_LEB_ID 0
#endif

uniform sampler2DArray tt_Textures[TT_TEXTURES_PER_PAGE];

layout(std430, binding = TT_BUFFER_BINDING_INDIRECTION)
readonly buffer tt_IndirectionBuffer {
    int tt_Indirections[2048];
};

vec4 tt_texture(int textureID, vec2 P);

void tt__TriangleToSquare(inout vec2 P)
{
    if (P.x < P.y) {
        P.y+= P.x;
        P.x*= 2.0f;
    } else {
        P.x+= P.y;
        P.y*= 2.0f;
    }
}

vec4 tt_texture(int textureID, vec2 P)
{
    vec2 Q;
    const int lebID = TT_LEB_ID;
    leb_Node node   = leb_BoundingNode_Quad(lebID, P, Q);
    uint bitID      = leb_EncodeNode(lebID, node);
    int layer       = tt_Indirections[bitID];

    tt__TriangleToSquare(Q);

    return texture(tt_Textures[textureID], vec3(Q, layer));
}

