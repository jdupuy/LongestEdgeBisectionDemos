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
    float z0 = texture(u_TerrainDmapSampler, P).r;
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
    float noise = SimplexPerlin2D(vec2(P) * 0.25);

    if (slopeMagSqr < 30.0f + noise && textureData.z > 1.5f) {
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
        vec3 albedo = vec3(160, 160, 104) / 255.0;
        //vec3 albedo = vec3(0, 0, 104) / 255.0;
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
    AddRock(textureData, Q);
    AddSand(textureData, Q);

    // finalize data
    vec3 N = normalize(vec3(-textureData.dzdx, -textureData.dzdy, 1));
    vec2 Np = ConcentricMapBwd(N.xy);

    imageStore(u_ChunkAmapSampler, P, vec4(textureData.albedo, 0));
    imageStore(u_ChunkDmapSampler, P, vec4(textureData.z, 0, 0, 0));
    imageStore(u_ChunkNmapSampler, P, vec4(Np, 0, 0));
}

#endif
