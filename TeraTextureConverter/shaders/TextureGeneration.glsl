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

/*
    Cubic texture filtering -- based on
    https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    by Matt Pettineo
*/
vec4 TextureCubic(sampler2D sampler, vec2 P)
{
#if 1
    vec2 texSize = vec2(textureSize(sampler, 0));
    vec2 uv = P;

    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 samplePos = P * texSize;
    vec2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    vec2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    vec2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    vec2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 texPos0 = texPos1 - 1;
    vec2 texPos3 = texPos1 + 2;
    vec2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    vec4 result = vec4(0.0f);

    result+= textureLod(sampler, vec2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result+= textureLod(sampler, vec2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result+= textureLod(sampler, vec2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result+= textureLod(sampler, vec2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result+= textureLod(sampler, vec2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result+= textureLod(sampler, vec2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result+= textureLod(sampler, vec2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result+= textureLod(sampler, vec2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result+= textureLod(sampler, vec2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
#else
    return texture(sampler, P);
#endif
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

        o_FragColor = TextureCubic(u_InputSampler, uv);
    } else {
        o_FragColor = vec4(1.0f);
    }
}
#endif
