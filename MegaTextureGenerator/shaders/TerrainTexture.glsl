/*
    This API is responsible for building a texture for a terrain using
    detail maps. The detail maps are applied onto a coarse displacement
    map. Each detail map is applied depending on the local properties
    of the coarse displacement. Specifically, we use altitude, slope,
    and roughness over arbitrary footprints. We apply three types of
    textures: grass, sand and rock.

 */

struct TT_Texel {
    float altitude;
    vec3 albedo;
    vec2 slope;
};

TT_Texel TT_TextureFetch(vec2 u);
TT_Texel TT_TextureFetchGrad(vec2 u, vec2 dudx, vec2 dudy);

///////// end header file //////////////////////////

#define TEXTURE_TERRAIN 0
#define TEXTURE_SAND    1
#define TEXTURE_GRASS   2
#define TEXTURE_ROCK    3
#define TEXTURE_COUNT   4

struct TT__TextureDimensions {
    float width, height, zMin, zMax;
};

uniform sampler2D TT_TerrainDisplacementSampler;
uniform sampler2D TT_DetailDisplacementSamplers[];
uniform sampler2D TT_DetailAlbedoSamplers[];
layout (std140, binding = WORLD_SPACE_TEXTURE_DIMENSIONS_BUFFER_BINDING)
uniform WorldSpaceTextureDimensionsBuffer {
    TT__TextureDimensions TT_WorldSpaceTextureDimensions[TEXTURE_COUNT];
};


/*
    Cubic texture filtering -- based on
    https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    by Matt Pettineo
*/
vec4 TT__TextureCubic(sampler2D sampler, vec2 P)
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

void
TT__ReMapDisplacementData(
    in const TT__TextureDimensions textureDimensions,
    inout float altitude,
    inout vec2 slope
) {
    float width  = textureDimensions.width;
    float height = textureDimensions.height;
    float zMin = textureDimensions.zMin;
    float zMax = textureDimensions.zMax;
    float scale = zMax - zMin;

    altitude = altitude * scale + zMin;
    slope.x*= scale / width;
    slope.y*= scale / height;
}

TT_Texel TT__TextureFetch_Terrain_Raw(vec2 u)
{
    TT_Texel texel;
    vec2 eps = 1.0f / vec2(textureSize(TT_TerrainDisplacementSampler, 0));
    float x1 = TT__TextureCubic(TT_TerrainDisplacementSampler, u - vec2(eps.x, 0)).r;
    float x2 = TT__TextureCubic(TT_TerrainDisplacementSampler, u + vec2(eps.x, 0)).r;
    float y1 = TT__TextureCubic(TT_TerrainDisplacementSampler, u - vec2(0, eps.y)).r;
    float y2 = TT__TextureCubic(TT_TerrainDisplacementSampler, u + vec2(0, eps.y)).r;

    texel.altitude = TT__TextureCubic(TT_TerrainDisplacementSampler, u).r;
    texel.slope.x = 0.5f * (x2 - x1) / eps.x;
    texel.slope.y = 0.5f * (y2 - y1) / eps.y;
    texel.albedo = vec3(0.0f);

    return texel;
}


TT_Texel TT__TextureFetch_Terrain(vec2 u)
{
    TT_Texel texel = TT__TextureFetch_Terrain_Raw(u);

    TT__ReMapDisplacementData(TT_WorldSpaceTextureDimensions[TEXTURE_TERRAIN],
                              texel.altitude, texel.slope);

    return texel;
}

// (footprintradius in worldspace)
float TT__TerrainCurvature(vec2 u, float footprintRadius)
{
    TT__TextureDimensions textureDimensions =
            TT_WorldSpaceTextureDimensions[TEXTURE_TERRAIN];
    float width  = textureDimensions.width;
    float height = textureDimensions.height;
    float zMin = textureDimensions.zMin;
    float zMax = textureDimensions.zMax;
    vec2 dudx = vec2(2.0f * footprintRadius / width, 0.0f);
    vec2 dudy = vec2(0.0f, 2.0f * footprintRadius / height);
    float curvature = textureGrad(TT_TerrainDisplacementSampler, u, dudx, dudy).g;
    float scale = 2.0f * (zMax - zMin) / (width + height);

    return curvature * scale;
}

float TT__FractalBrownianMotion(vec2 u, int octaveCount = 10)
{
    float fbm = 0.0f;
    float octaveBegin = 0;
    float octaveEnd   = float(octaveCount);
    float bound = exp2(-octaveBegin) - exp2(-octaveEnd);

    for (float octave = octaveBegin; octave < octaveEnd; ++octave) {
        float sc = exp2(octave);

        fbm+= SimplexPerlin2D(u * sc) / sc;
    }

    return fbm / bound * 0.5f + 0.5f;
}

vec3 TT__FractalBrownianMotionDeriv(vec2 u, int octaveCount = 14)
{
    vec3 fbm = vec3(0.0f);
    float octaveBegin = 0;
    float octaveEnd   = float(octaveCount);
    float bound = exp2(-octaveBegin) - exp2(-octaveEnd);

    for (float octave = octaveBegin; octave < octaveEnd; ++octave) {
        float sc = exp2(octave);

        fbm+= SimplexPerlin2D_Deriv(u * sc) / vec3(sc);
    }

    return fbm / bound * 0.5f + 0.5f;
}

float TT__SimplexNoise(vec2 u)
{
    return SimplexPerlin2D(u) * 0.5f + 0.5f;
}

float TT__Sqr(float x) {return x * x;}

// add procedural displacement based on slope and curvature
void TT__AddNoise(vec2 u, inout TT_Texel texel)
{
    float slopeSqr = dot(texel.slope, texel.slope);
    vec2 slopeDir = texel.slope / 100.0;// * inversesqrt(slopeSqr);
    float noiseFrequency = 512.0f;
    float noiseScale = 0.125f * smoothstep(0.0, 0.25, slopeSqr);
    vec3 noise = TT__FractalBrownianMotionDeriv(u * noiseFrequency, 5) * noiseScale;

#if 1
    texel.altitude+= noise.x;
    texel.slope+= noise.yz;
#endif
}

// lookup detail texture map
TT_Texel TT__TextureFetch_Detail(int textureID, vec2 u)
{
    TT__TextureDimensions terrainDimensions =
            TT_WorldSpaceTextureDimensions[TEXTURE_TERRAIN];
    TT__TextureDimensions detailDimensions =
            TT_WorldSpaceTextureDimensions[textureID];
    vec2 sc = vec2(terrainDimensions.width / detailDimensions.width,
                   terrainDimensions.height / detailDimensions.height);
    int samplerID = textureID - 1;
    vec2 P = sc * u;
    vec2 eps = 1.0f / vec2(textureSize(TT_DetailDisplacementSamplers[samplerID], 0));
    float x1 = textureLod(TT_DetailDisplacementSamplers[samplerID], P - vec2(eps.x, 0), 0.0).r;
    float x2 = textureLod(TT_DetailDisplacementSamplers[samplerID], P + vec2(eps.x, 0), 0.0).r;
    float y1 = textureLod(TT_DetailDisplacementSamplers[samplerID], P - vec2(0, eps.y), 0.0).r;
    float y2 = textureLod(TT_DetailDisplacementSamplers[samplerID], P + vec2(0, eps.y), 0.0).r;
    TT_Texel texel;

    texel.altitude = texture(TT_DetailDisplacementSamplers[samplerID], P).r;
    texel.slope.x = 0.5f * (x2 - x1) / eps.x;
    texel.slope.y = 0.5f * (y2 - y1) / eps.y;
    texel.albedo = texture(TT_DetailAlbedoSamplers[samplerID], P).rgb;
    TT__ReMapDisplacementData(TT_WorldSpaceTextureDimensions[textureID],
                              texel.altitude, texel.slope);

    return texel;
}


#define WATER_LEVEL     0.0f
#define SAND_LEVEL      1.0f

void TT_AddWater(vec2 u, inout TT_Texel texel)
{
    float urng = 0.0*TT__FractalBrownianMotion(u * 1e4) + 0.5;

    if (texel.altitude - urng <= WATER_LEVEL) {
        texel.albedo = vec3(0, 0, 1);
    }
}

void TT_AddSand(vec2 u, inout TT_Texel texel)
{
    float urng = 0.0*TT__FractalBrownianMotion(u * 1e4);

    if (texel.altitude < SAND_LEVEL + urng) {
        TT_Texel tmp = TT__TextureFetch_Detail(TEXTURE_SAND, u);

        texel.altitude+= tmp.altitude;
        texel.slope+= tmp.slope;
        texel.albedo = tmp.albedo;
    }
}

void TT_AddGrass(vec2 u, inout TT_Texel texel)
{
    float slopeSqr = dot(texel.slope, texel.slope);
    float urng = -0.15*TT__FractalBrownianMotion(u * 1e2);

    if (slopeSqr <= 0.5f + urng && texel.altitude >= SAND_LEVEL + urng) {
        TT_Texel tmp = TT__TextureFetch_Detail(TEXTURE_GRASS, u);

        texel.altitude+= tmp.altitude;
        texel.slope+= tmp.slope;
        texel.albedo = tmp.albedo;
    }
}

void TT_AddRock(vec2 u, inout TT_Texel texel)
{
    float slopeSqr = dot(texel.slope, texel.slope);
    float urng = -0.15*TT__FractalBrownianMotion(u);

    if (slopeSqr > 0.5f + urng && texel.altitude >= SAND_LEVEL + urng) {
        TT_Texel tmp = TT__TextureFetch_Detail(TEXTURE_ROCK, u);

        texel.altitude+= tmp.altitude;
        texel.slope+= tmp.slope;
        texel.albedo = 1.5 * tmp.albedo;
    }
}

TT_Texel TT_TextureFetch(vec2 u)
{
    // Lookup terrain
    TT_Texel texel = TT__TextureFetch_Terrain(u);
    TT__AddNoise(u, texel);

    // Add details maps
    //texel.albedo = vec3(0, 1, 1);
    TT_AddRock(u, texel);
    TT_AddGrass(u, texel);
    TT_AddSand(u, texel);
    TT_AddWater(u, texel);
#if 0
#endif
/*
    float slopeMag = dot(texel.slope, texel.slope);
    float roughness = TT__TerrainRoughness(u, 25.0f);
    if (slopeMag > 1.0f + SimplexPerlin2D(u * 86254.0f) * 0.5f)
        texel.albedo = texture(
            TT_DetailAlbedoSamplers[1],
            u * TT_WorldSpaceTextureDimensions[0].width /
                TT_WorldSpaceTextureDimensions[2].width ).rgb;
    else if (roughness > 0.01f)
        texel.albedo = texture(
            TT_DetailAlbedoSamplers[0],
            u * TT_WorldSpaceTextureDimensions[0].width /
                TT_WorldSpaceTextureDimensions[1].width ).rgb;
*/
    return texel;
}
