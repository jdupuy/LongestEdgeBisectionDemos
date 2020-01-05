




// chunk data
uniform ivec2 u_ChunkCoordinate;
uniform int u_ChunkZoomFactor;
uniform int u_ChunkResolution;
uniform int u_MegaTextureResolution;

// data from original displacement map
uniform sampler2D u_TerrainDmapSampler;
uniform int u_TerrainDmapResolution;
uniform vec2 u_TerrainDmapZminZmax;
uniform float u_TerrainDmapSize; // size in meters

// detail texture map data
uniform sampler2D u_RockDmapSampler;
uniform sampler2D u_RockAmapSampler;
uniform sampler2D u_GrassDmapSampler;
uniform sampler2D u_GrassAmapSampler;
uniform float u_GrassFrequency;
uniform float u_RockFrequency;

// data to pack
struct TextureData {
    float z;
    float dzdx;
    float dzdy;
    vec3 albedo;
};

vec2 texelCoordToTerrainTextureCoord(ivec2 P)
{
    return vec2(dvec2(P / double(u_MegaTextureResolution)));
}

float LookupTerrainDisplacement(vec2 P)
{
#if 0
    float z0 = textureLod(u_TerrainDmapSampler, P, 0.0).r;
#else

    vec2 texSize = vec2(textureSize(u_TerrainDmapSampler, 0));
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

    float result = 0.0f;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos0.x, texPos0.y), 0.0f).r * w0.x * w0.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos12.x, texPos0.y), 0.0f).r * w12.x * w0.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos3.x, texPos0.y), 0.0f).r * w3.x * w0.y;

    result+= textureLod(u_TerrainDmapSampler, vec2(texPos0.x, texPos12.y), 0.0f).r * w0.x * w12.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos12.x, texPos12.y), 0.0f).r * w12.x * w12.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos3.x, texPos12.y), 0.0f).r * w3.x * w12.y;

    result+= textureLod(u_TerrainDmapSampler, vec2(texPos0.x, texPos3.y), 0.0f).r * w0.x * w3.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos12.x, texPos3.y), 0.0f).r * w12.x * w3.y;
    result+= textureLod(u_TerrainDmapSampler, vec2(texPos3.x, texPos3.y), 0.0f).r * w3.x * w3.y;

    float z0 = result;
#endif
    float size = u_TerrainDmapZminZmax.y - u_TerrainDmapZminZmax.x;
    return z0 * size + u_TerrainDmapZminZmax.x;
}


TextureData LoadTerrainTextureData(ivec2 P)
{
    const float eps = 1.0f / float(u_TerrainDmapResolution);
    vec2 Q = texelCoordToTerrainTextureCoord(P);
    float z1 = LookupTerrainDisplacement(Q - vec2(eps, 0));
    float z2 = LookupTerrainDisplacement(Q + vec2(eps, 0));
    float z3 = LookupTerrainDisplacement(Q - vec2(0, eps));
    float z4 = LookupTerrainDisplacement(Q + vec2(0, eps));
    TextureData textureData;

    textureData.z = LookupTerrainDisplacement(Q);
    textureData.dzdx = (z2 - z1) / eps * 0.5f / u_TerrainDmapSize;
    textureData.dzdy = (z3 - z4) / eps * 0.5f / u_TerrainDmapSize;
    textureData.albedo = vec3(0);

    return textureData;
}

// grass node
void AddGrass(inout TextureData textureData, ivec2 P)
{
    float slopeMagSqr = textureData.dzdx * textureData.dzdx
                      + textureData.dzdy * textureData.dzdy;
    float noise = SimplexPerlin2D(vec2(P) * 0.25) * 0.0f;

    if (slopeMagSqr < 20.0f + noise && textureData.z > 1.5f) {
        const float eps = 1.0f / float(u_TerrainDmapResolution);
        vec2 Q = texelCoordToTerrainTextureCoord(P) * u_GrassFrequency;
        vec3 albedo = vec3(125, 145, 66) / 255.0;
        //albedo = textureLod(u_GrassAmapSampler, Q, 100.0).rgb;

        textureData.albedo+= albedo;
    }
}

// rock node
void AddRock(inout TextureData textureData, ivec2 P)
{
    float slopeMagSqr = textureData.dzdx * textureData.dzdx
                      + textureData.dzdy * textureData.dzdy;
    float noise = SimplexPerlin2D(vec2(P) * 0.25);

    if (slopeMagSqr >= 30.0f + noise && textureData.z > 1.5f) {
        const float eps = 1.0f / float(u_TerrainDmapResolution);
        vec2 Q = texelCoordToTerrainTextureCoord(P) * u_RockFrequency;
        vec3 albedo = vec3(92, 92, 76) / 255.0;
        //albedo = textureLod(u_RockAmapSampler, Q, 100.0).rgb;

        textureData.albedo+= albedo;
    }
}

// sand node
void AddSand(inout TextureData textureData, ivec2 P)
{
    float slopeMagSqr = textureData.dzdx * textureData.dzdx
                      + textureData.dzdy * textureData.dzdy;
    float noise = SimplexPerlin2D(vec2(P) * 64.0) * 0.0;

    if (textureData.z < 1.5f) {
        const float eps = 1.0f / float(u_TerrainDmapResolution);
        vec2 Q = texelCoordToTerrainTextureCoord(P) * u_RockFrequency;
        //vec3 albedo = vec3(160, 160, 104) / 255.0;
        vec3 albedo = vec3(0, 0, 104) / 255.0;
        //texture(u_RockAmapSampler, Q).rgb;

        textureData.albedo+= albedo;
    }
}


// map a point on the unit disk onto the unit square
vec2 ConcentricMapBwd(vec2 d)
{
    const float PI = 3.14159265359f;
    float r = sqrt(d.x * d.x + d.y * d.y);
    float phi = atan(d.y, d.x);
    float a, b;

    if (phi < -PI / 4.0f) {
        phi += 2.0f * PI;
    }

    if (phi < PI / 4.0f) {
        a = r;
        b = phi * a / (PI / 4.0f);
    } else if (phi < 3.0f * PI / 4.0f) {
        b = r;
        a = -(phi - PI / 2.0f) * b / (PI / 4.0f);
    } else if (phi < 5.0f * PI / 4.0f) {
        a = -r;
        b = (phi - PI) * a / (PI / 4.0f);
    } else {
        b = -r;
        a = -(phi - 3.0f * PI / 2.0f) * b / (PI / 4.0f);
    }

    return (vec2(a, b) + vec2(1.0f)) / 2.0f;
}

vec2 CompressNormal(in TextureData textureData)
{
    vec3 N = normalize(vec3(-textureData.dzdx, -textureData.dzdy, 1));
    return ConcentricMapBwd(N.xy);
}

float CompressDisplacement(in TextureData textureData)
{
    float zmin = u_TerrainDmapZminZmax.x;
    float zmax = u_TerrainDmapZminZmax.y;

    return (textureData.z - zmin) / (zmax - zmin);
}


#ifdef COMPUTE_SHADER
layout(local_size_x = 32,
       local_size_y = 32,
       local_size_z = 1) in;

layout(r16)     uniform image2D u_ChunkDmapSampler;
layout(rg8)     uniform image2D u_ChunkNmapSampler;
layout(rgba8)   uniform image2D u_ChunkAmapSampler;

void main()
{
    ivec2 P = ivec2(gl_GlobalInvocationID.xy);

    if (P.x >= u_ChunkResolution || P.y >= u_ChunkResolution)
        return;

    ivec2 Q = P * u_ChunkZoomFactor + u_ChunkCoordinate;

    // build data
    TextureData textureData = LoadTerrainTextureData(Q);
    AddGrass(textureData, Q);
    //AddRock(textureData, Q);
    AddSand(textureData, Q);

    // finalize data
    vec3 N = normalize(vec3(-textureData.dzdx, -textureData.dzdy, 1));
    vec2 Np = ConcentricMapBwd(N.xy);

    imageStore(u_ChunkAmapSampler, P, vec4(textureData.albedo, 0));
    imageStore(u_ChunkDmapSampler, P, vec4(CompressDisplacement(textureData), 0, 0, 0));
    imageStore(u_ChunkNmapSampler, P, vec4(CompressNormal(textureData), 0, 0));
}

#endif
