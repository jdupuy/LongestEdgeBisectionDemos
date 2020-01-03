
#ifndef TT_LEB_ID
#define TT_LEb_ID 0
#endif

uniform sampler2DArray tt_Textures[TT_TEXTURES_PER_PAGE];

layout(binding = TT_BUFFER_BINDING_INDIRECTION_MAP)
uniform tt_IndirectionMap {
    int tt_Indirections[2048];
};

vec4 tt_texture(int textureID, vec2 P);

void triangleToSquare(inout vec2 p)
{
    if (p.x < p.y) {
        p.y+= p.x;
        p.x*= 2.0f;
    } else {
        p.x+= p.y;
        p.y*= 2.0f;
    }
}

vec4 tt_texture(int textureID, vec2 P)
{
    const int lebID = TT_LEB_ID;
    leb_Node node   = leb_BoundingNode_Quad(lebID, texCoord, P);
    uint bitID      = leb_EncodeNode(lebID, node);
    int layer       = tt_Indirections[bitID];

    triangleToSquare(P);

    return texture(tt_Textures[textureID], vec3(P, layer));
}

