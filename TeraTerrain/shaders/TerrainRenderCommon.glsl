
/*******************************************************************************
 * Uniform Data -- Global variables for terrain rendering
 *
 */
layout(std140, column_major, binding = BUFFER_BINDING_TERRAIN_VARIABLES)
uniform PerFrameVariables {
    mat4 u_ModelViewMatrix;
    mat4 u_ModelViewProjectionMatrix;
    vec4 u_FrustumPlanes[6];
};

uniform float u_TargetEdgeLength;
uniform float u_LodFactor;
#if FLAG_DISPLACE
uniform sampler2D u_DmapSampler;
uniform sampler2D u_SmapSampler;
uniform float u_DmapFactor;
uniform float u_MinLodVariance;
#endif


/*******************************************************************************
 * DecodeTriangleVertices -- Decodes the triangle vertices in local space
 *
 */
vec4[3] DecodeTriangleVertices(in const leb_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
    vec4 p1 = vec4(pos[0][0], pos[1][0], 0.0, 1.0);
    vec4 p2 = vec4(pos[0][1], pos[1][1], 0.0, 1.0);
    vec4 p3 = vec4(pos[0][2], pos[1][2], 0.0, 1.0);

#if FLAG_DISPLACE
    p1.z = u_DmapFactor * texture(u_DmapSampler, p1.xy).r;
    p2.z = u_DmapFactor * texture(u_DmapSampler, p2.xy).r;
    p3.z = u_DmapFactor * texture(u_DmapSampler, p3.xy).r;
#endif

    return vec4[3](p1, p2, p3);
}

/*******************************************************************************
 * TriangleLevelOfDetail -- Computes the LoD assocaited to a triangle
 *
 * This function is used to garantee a user-specific pixel edge length in
 * screen space. The reference edge length is that of the longest edge of the
 * input triangle.In practice, we compute the LoD as:
 *      LoD = 2 * log2(EdgePixelLength / TargetPixelLength)
 * where the factor 2 is because the number of segments doubles every 2
 * subdivision level.
 */
float TriangleLevelOfDetail_Perspective(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;

#if 0 //  human-readable version
    vec3 edgeCenter = (v0 + v2); // division by 2 was moved to u_LodFactor
    vec3 edgeVector = (v2 - v0);
    float distanceToEdgeSqr = dot(edgeCenter, edgeCenter);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u_LodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
#else // optimized version
    float sqrMagSum = dot(v0, v0) + dot(v2, v2);
    float twoDotAC = 2.0f * dot(v0, v2);
    float distanceToEdgeSqr = sqrMagSum + twoDotAC;
    float edgeLengthSqr     = sqrMagSum - twoDotAC;

    return u_LodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
#endif
}

/*
    In Orthographic Mode, we have
        EdgePixelLength = EdgeViewSpaceLength / ImagePlaneViewSize * ImagePlanePixelResolution
    and so using some identities we get:
        LoD = 2 * (log2(EdgeViewSpaceLength)
            + log2(ImagePlanePixelResolution / ImagePlaneViewSize)
            - log2(TargetPixelLength))

            = log2(EdgeViewSpaceLength^2)
            + 2 * log2(ImagePlanePixelResolution / (ImagePlaneViewSize * TargetPixelLength))
    so we precompute:
    u_LodFactor = 2 * log2(ImagePlanePixelResolution / (ImagePlaneViewSize * TargetPixelLength))
*/
float TriangleLevelOfDetail_Orthographic(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u_LodFactor + log2(edgeLengthSqr);
}

vec3 Inverse(vec3 x) {return x / dot(x, x);}
vec3 StereographicProjection(vec3 x) {
    const vec3 center = vec3(0.0f, 0.0f, 1.0f);

    return 2.0f * Inverse(x + center) - center;
}
vec3 ViewSpaceToScreenSpace(vec3 x)
{
    // project onto unit sphere
    float nrmSqr = dot(x, x);
    float nrm = inversesqrt(nrmSqr);
    vec3 xNrm = x * nrm;

    // project onto screen
    vec2 xNdc = StereographicProjection(xNrm).xy;
    return vec3(xNdc, nrmSqr);
}

float TriangleLevelOfDetail_Fisheye(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u_LodFactor + log2(edgeLengthSqr);
}

float TriangleLevelOfDetail(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;
#if defined(PROJECTION_RECTILINEAR)
    return TriangleLevelOfDetail_Perspective(patchVertices);
#elif defined(PROJECTION_ORTHOGRAPHIC)
    return TriangleLevelOfDetail_Orthographic(patchVertices);
#elif defined(PROJECTION_FISHEYE)
    return TriangleLevelOfDetail_Perspective(patchVertices);
#else
    return 0.0;
#endif
}

#if FLAG_DISPLACE
/*******************************************************************************
 * DisplacementVarianceTest -- Checks if the height variance criteria is met
 *
 * Terrains tend to have locally flat regions, which don't need large amounts
 * of polygons to be represented faithfully. This function checks the
 * local flatness of the terrain.
 *
 */
bool DisplacementVarianceTest(in const vec4[3] patchVertices)
{
#define P0 patchVertices[0].xy
#define P1 patchVertices[1].xy
#define P2 patchVertices[2].xy
    vec2 P = (P0 + P1 + P2) / 3.0;
    vec2 dx = (P0 - P1);
    vec2 dy = (P2 - P1);
    vec2 dmap = textureGrad(u_DmapSampler, P, dx, dy).rg;
    float dmapVariance = clamp(dmap.y - dmap.x * dmap.x, 0.0, 1.0);

    return (dmapVariance >= u_MinLodVariance);
#undef P0
#undef P1
#undef P2
}
#endif

/*******************************************************************************
 * FrustumCullingTest -- Checks if the triangle lies inside the view frutsum
 *
 * This function depends on FrustumCulling.glsl
 *
 */
bool FrustumCullingTest(in const vec4[3] patchVertices)
{
    vec3 bmin = min(min(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);
    vec3 bmax = max(max(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);
#   if FLAG_DISPLACE
    bmin.z = 0.0;
    bmax.z = u_DmapFactor;
#   endif

    return FrustumCullingTest(u_FrustumPlanes, bmin, bmax);
}

/*******************************************************************************
 * LevelOfDetail -- Computes the level of detail of associated to a triangle
 *
 * The first component is the actual LoD value. The second value is 0 if the
 * triangle is culled, and one otherwise.
 *
 */
vec2 LevelOfDetail(in const vec4[3] patchVertices)
{
    // culling test
    if (!FrustumCullingTest(patchVertices))
#if FLAG_CULL
        return vec2(0.0f, 0.0f);
#else
        return vec2(0.0f, 1.0f);
#endif

#   if FLAG_DISPLACE
    // variance test
    if (!DisplacementVarianceTest(patchVertices))
        return vec2(0.0f, 1.0f);
#endif

    // compute triangle LOD
    return vec2(TriangleLevelOfDetail(patchVertices), 1.0f);
}


/*******************************************************************************
 * BarycentricInterpolation -- Computes a barycentric interpolation
 *
 */
vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

vec4 BarycentricInterpolation(in vec4 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}


/*******************************************************************************
 * GenerateVertex -- Computes the final vertex position
 *
 */
struct ClipSpaceAttribute {
    vec4 position;
    vec2 texCoord;
};

ClipSpaceAttribute TessellateClipSpaceTriangle(
    in const vec4 vertexPositions[3],
    in const vec2 vertexTexCoords[3],
    in vec2 tessellationCoordinate
) {
    // compute final attributes
    vec4 position = BarycentricInterpolation(vertexPositions, tessellationCoordinate);
    vec2 texCoord = BarycentricInterpolation(vertexTexCoords, tessellationCoordinate);

#if FLAG_DISPLACE
    // displace the surface in clip space
    vec4 upDir = u_ModelViewProjectionMatrix[2];
    float z = u_DmapFactor * textureLod(u_DmapSampler, texCoord, 0.0).r;

    position+= upDir * z;
#endif

    return ClipSpaceAttribute(position, texCoord);
}


/*******************************************************************************
 * ShadeFragment -- Fragement shading routine
 *
 */
#ifdef FRAGMENT_SHADER
vec4 ShadeFragment(vec2 texCoord)
{
#if FLAG_DISPLACE
    vec2 smap = texture(u_SmapSampler, texCoord).rg * u_DmapFactor;
    vec3 n = normalize(vec3(-smap, 1));
#else
    vec3 n = vec3(0, 0, 1);
#endif

#if SHADING_SNOWY
    float d = clamp(n.z, 0.0, 1.0);
    float slopeMag = dot(n.xy, n.xy);
    vec3 albedo = slopeMag > 0.5 ? vec3(0.75) : vec3(2);
    float z = 3.0 * gl_FragCoord.z / gl_FragCoord.w;

    return vec4(mix(vec3(albedo * d / 3.14159), vec3(0.5), 1.0 - exp2(-z)), 1);
#elif SHADING_DIFFUSE
    vec2 P = texCoord;
    vec3 albedo = tt_texture(0, P).rgb;

    return vec4(vec3(clamp(n.z, 0.0, 1.0) / 3.14159) * albedo * 8.0, 1);
#elif SHADING_NORMALS

    return vec4(abs(n), 1);
#elif SHADING_COLOR

    return vec4(1, 1, 1, 1);
#else
    return vec4(1, 0, 0, 1);
#endif
}
#endif
