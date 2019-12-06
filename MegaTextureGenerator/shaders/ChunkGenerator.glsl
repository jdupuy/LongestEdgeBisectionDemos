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

//
struct TerrainData {
    float z;
    float dzdx;
    float dzdy;
};

vec2 texelCoordToTerrainTextureCoord(ivec2 P)
{
    return vec2(dvec2(P / double(u_MegaTextureResolution)));
}

float LookupTerrainDisplacement(vec2 P)
{
    float z0 = texture(u_TerrainDmapSampler, P).r;
    float size = u_TerrainDmapZminZmax.y - u_TerrainDmapZminZmax.x;
    return z0 * size - u_TerrainDmapZminZmax.x;
}

TerrainData LoadTerrainData(ivec2 P)
{
    const float eps = 1.0f / float(u_TerrainDmapResolution);
    vec2 Q = texelCoordToTerrainTextureCoord(P);
    float z1 = LookupTerrainDisplacement(Q - vec2(eps, 0));
    float z2 = LookupTerrainDisplacement(Q + vec2(eps, 0));
    float z3 = LookupTerrainDisplacement(Q - vec2(0, eps));
    float z4 = LookupTerrainDisplacement(Q + vec2(0, eps));
    TerrainData data;

    data.z = LookupTerrainDisplacement(Q);
    data.dzdx = (z2 - z1) / eps * 0.5f / u_TerrainDmapSize;
    data.dzdy = (z3 - z4) / eps * 0.5f / u_TerrainDmapSize;

    return data;
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

    // write data
    TerrainData terrainData = LoadTerrainData(Q);
    vec3 N = normalize(vec3(-terrainData.dzdx, -terrainData.dzdy, 1));

    imageStore(u_ChunkAmapSampler, P, vec4(terrainData.z, 0, 0, 0));
    imageStore(u_ChunkDmapSampler, P, vec4(terrainData.z, 0, 0, 0));
    imageStore(u_ChunkNmapSampler, P, vec4(N.xy * 0.5f + 0.5f, 0, 0));
}

#endif
