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

int xorshift(in int value) {
    // Xorshift*32
    // Based on George Marsaglia's work: http://www.jstatsoft.org/v08/i14/paper
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

int nextInt(inout int seed) {
    seed = xorshift(seed);
    return seed;
}

float nextFloat(inout int seed) {
    seed = xorshift(seed);
    // FIXME: This should have been a seed mapped from MIN..MAX to 0..1 instead
    return abs(fract(float(seed) / 3141.592653));
}

float nextFloat(inout int seed, in float max) {
    return nextFloat(seed) * max;
}

float checkersTexture( in vec2 p )
{
    vec2 q = floor(p);
    return mod( q.x + q.y, 2.0 );            // xor pattern
}

vec4 tt_texture(int textureID, vec2 P)
{
    vec2 Q;
    const int lebID = TT_LEB_ID;
    leb_Node node   = leb_BoundingNode_Quad(lebID, P, Q);
    uint bitID      = leb_EncodeNode(lebID, node);
    int layer       = tt_Indirections[bitID];

    tt__TriangleToSquare(Q);

    int rngSeed = int(node.id);
#if 0
    vec4 colors[8] = vec4[8](
        vec4(0.86,0.00,0.00, 0),
        vec4(0.00,0.20,0.70, 0),
        vec4(0.10,0.50,0.10, 0),
        vec4(0.40,0.40,0.40, 0),
        vec4(1.00,0.50,0.00, 0),
        vec4(1.00,1.00,1.00, 0),
        vec4(0.00,0.50,1.00, 0),
        vec4(0.00,1.00,0.50, 0)
    );
    return colors[node.id % 8];
#else
    float c = checkersTexture(Q * 32.0) * 0.03 + 0.97;
    //return c * vec4(nextFloat(rngSeed), nextFloat(rngSeed), nextFloat(rngSeed), 0);
#endif

    return texture(tt_Textures[textureID], vec3(Q, layer));
}

