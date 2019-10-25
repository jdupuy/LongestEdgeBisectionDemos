/* render.glsl - public domain
by Jonathan Dupuy

    This code has dependencies on the following GLSL sources:
    - TerrainRenderCommon.glsl
    - LongestEdgeBisection.glsl
*/

layout(binding = BUFFER_BINDING_LEB_NODE_COUNTER)
uniform atomic_uint u_NodeCounter;

layout(std430, binding = BUFFER_BINDING_LEB_NODE_BUFFER)
buffer NodeBuffer {
    uint u_LebNodeBuffer[];
    //mat4 u_LebNodeBuffer[];
};

#ifdef COMPUTE_SHADER
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

void writeNodeID(uint NodeID)
{
    uint index = atomicCounterIncrement(u_NodeCounter);

    u_LebNodeBuffer[index] = NodeID;
}

void main(void)
{
    // get threadID
    uint threadID = gl_GlobalInvocationID.x;

    if (threadID < leb_NodeCount()) {
        // and extract triangle vertices
        leb_Node node = leb_DecodeNode(threadID);
        vec4 triangleVertices[3] = DecodeTriangleVertices(node);

        // compute target LoD
        vec2 targetLod = LevelOfDetail(triangleVertices);

        // splitting update
#if FLAG_SPLIT
        if (targetLod.x > 1.0) {
            leb_SplitNodeConforming(node);
        }
#endif

#if FLAG_MERGE
        if (true) {
            leb_DiamondParent diamond = leb_DecodeDiamondParent(node);
            bool shouldMergeBase = LevelOfDetail(DecodeTriangleVertices(diamond.base)).x < 1.0;
            bool shouldMergeTop = LevelOfDetail(DecodeTriangleVertices(diamond.top)).x < 1.0;

            if (shouldMergeBase && shouldMergeTop) {
                leb_MergeNodeConforming(node, diamond);
            }
        }
#endif

        // push node to stack if it's visible
        if (targetLod.y > 0.0) {
            writeNodeID(node.id);
        }
    }
}
#endif

