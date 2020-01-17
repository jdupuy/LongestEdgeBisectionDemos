/* lebUpdate
by Jonathan Dupuy

*/

uniform sampler2D u_DensitySampler;
uniform float u_TargetVariance;

/*******************************************************************************
 * DecodeTriangleVertices -- Decodes the triangle vertices in local space
 *
 */
vec2[3] DecodeTriangleVertices(in const leb_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
    vec2 p1 = vec2(pos[0][0], pos[1][0]);
    vec2 p2 = vec2(pos[0][1], pos[1][1]);
    vec2 p3 = vec2(pos[0][2], pos[1][2]);

    return vec2[3](p1, p2, p3);
}

vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

float LevelOfDetail(in const leb_Node node)
{
    vec2 triangleVertices[3] = DecodeTriangleVertices(node);
    vec2 P = BarycentricInterpolation(triangleVertices, vec2(0.333333f));
    float textureLodCount = float(textureQueryLevels(u_DensitySampler));
    float lodOffset = float(node.depth - 1.0f) / 2.0f;
    vec2 texel = textureLod(u_DensitySampler, P, textureLodCount - lodOffset).rg;
    float var = max(texel.g - texel.r * texel.r, 0.0f);

    if (var > u_TargetVariance) {
        return  2.0f;
    } else if (4.0f * var < u_TargetVariance) {
        return -2.0f;
    } else {
        return  0.0f;
    }
}

#ifdef COMPUTE_SHADER
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

void main(void)
{
    // get threadID
    const int lebID = 0;
    uint threadID = gl_GlobalInvocationID.x;

    if (threadID < leb_NodeCount(lebID)) {
        // and extract triangle vertices
        leb_Node node = leb_DecodeNode(lebID, threadID);

        // compute target LoD
        float targetLod = LevelOfDetail(node);

        // splitting update
#if FLAG_SPLIT
        if (targetLod > 1.0) {
            leb_SplitNodeConforming_Quad(lebID, node);
        }
#endif

#if FLAG_MERGE
        if (true) {
            leb_DiamondParent diamond = leb_DecodeDiamondParent_Quad(node);
            bool shouldMergeBase = LevelOfDetail(diamond.base) < 0.0;
            bool shouldMergeTop = LevelOfDetail(diamond.top) < 0.0;

            if (shouldMergeBase && shouldMergeTop) {
                leb_MergeNodeConforming_Quad(lebID, node, diamond);
            }
        }
#endif
    }
}
#endif

