#line 1
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std140, column_major, binding = BUFFER_BINDING_PARAMETERS)
uniform Parameters {
    mat4 u_ModelViewMatrix;
    vec4 u_FrustumPlanes[6];
    vec2 u_LodFactor;
    float u_TargetEdgeLength;
    //float align[5];
};

/*******************************************************************************
 * Negative Vertex of an AABB
 *
 * This procedure computes the negative vertex of an AABB
 * given a normal.
 * See the View Frustum Culling tutorial @ LightHouse3D.com
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes-ii/
 *
 */
vec3 NegativeVertex(vec3 bmin, vec3 bmax, vec3 n)
{
    bvec3 b = greaterThan(n, vec3(0));
    return mix(bmin, bmax, b);
}

/*******************************************************************************
 * Frustum-AABB Culling Test
 *
 * This procedure returns true if the AABB is either inside, or in
 * intersection with the frustum, and false otherwise.
 * The test is based on the View Frustum Culling tutorial @ LightHouse3D.com
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes-ii/
 *
 */
bool FrustumCullingTest(vec3 bmin, vec3 bmax)
{
    float a = 1.0f;

    for (int i = 0; i < 6 && a >= 0.0f; ++i) {
        vec3 n = NegativeVertex(bmin, bmax, u_FrustumPlanes[i].xyz);

        a = dot(vec4(n, 1.0f), u_FrustumPlanes[i]);
    }

    return (a >= 0.0);
}


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
 *
 */
float TriangleLevelOfDetail(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;
    float sqrMagSum = dot(v0, v0) + dot(v2, v2);
    float twoDotAC = 2.0f * dot(v0, v2);
    float distanceToEdgeSqr = sqrMagSum + twoDotAC;
    float edgeLengthSqr     = sqrMagSum - twoDotAC;

    return u_LodFactor.x + log2(edgeLengthSqr)
            - u_LodFactor.y * log2(distanceToEdgeSqr);
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

    return FrustumCullingTest(bmin, bmax);
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
        return vec2(0.0f, 0.0f);

    // compute triangle LOD
    return vec2(TriangleLevelOfDetail(patchVertices), 1.0f);
}


/*******************************************************************************
 * Main
 *
 */
void main(void)
{
    // get threadID
    const int lebID = 0;
    uint threadID = gl_GlobalInvocationID.x;

    if (threadID < leb_NodeCount(lebID)) {
        // and extract triangle vertices
        leb_Node node = leb_DecodeNode(lebID, threadID);
        vec4 triangleVertices[3] = DecodeTriangleVertices(node);

        // compute target LoD
        vec2 targetLod = LevelOfDetail(triangleVertices);

        // splitting update
#if FLAG_SPLIT
        if (targetLod.x > 1.0) {
            leb_SplitNodeConforming_Quad(lebID, node);
        }
#endif

#if FLAG_MERGE
        if (true) {
            leb_DiamondParent diamond = leb_DecodeDiamondParent_Quad(node);
            bool shouldMergeBase = LevelOfDetail(DecodeTriangleVertices(diamond.base)).x < 1.0;
            bool shouldMergeTop = LevelOfDetail(DecodeTriangleVertices(diamond.top)).x < 1.0;

            if (shouldMergeBase && shouldMergeTop) {
                leb_MergeNodeConforming_Quad(lebID, node, diamond);
            }
        }
#endif
    }
}
