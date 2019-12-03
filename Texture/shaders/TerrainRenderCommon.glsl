
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
uniform sampler2DArray u_LebTexture;

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

    return ClipSpaceAttribute(position, texCoord);
}


/*******************************************************************************
 * ShadeFragment -- Fragement shading routine
 *
 */
#ifdef FRAGMENT_SHADER
void triangleToSquare(inout vec2 x)
{
    if (x.y > x.x) {
        x.y+= x.x;
        x.x*= 2.0f;
    } else {
        x.x+= x.y;
        x.y*= 2.0f;
    }
}

vec4 ShadeFragment(vec2 texCoord)
{
    const int lebID = 0;
    leb_Node node = leb_BoundingNode_Quad(lebID, texCoord);
    vec4[3] triangleVertices = DecodeTriangleVertices(node);
    vec2 P = BarycentricInterpolation(triangleVertices, texCoord).xy;

    triangleToSquare(texCoord);
    triangleToSquare(P);
    vec4 colorData[4] = vec4[4](
        vec4(1, 0, 0, 1),
        vec4(1, 1, 0, 1),
        vec4(1, 0, 1, 1),
        vec4(0, 1, 0, 1)
    );
    //return colorData[node.id % 4];
    return texture(u_LebTexture, vec3(P, node.id));
}
#endif
