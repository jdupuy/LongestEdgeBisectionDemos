uniform int u_PassID;

#ifdef COMPUTE_SHADER
layout (local_size_x = 256,
        local_size_y = 1,
        local_size_z = 1) in;

void main(void)
{
    uint cnt = (1u << u_PassID);
#ifdef LEB_REDUCTION_PREPASS
    uint threadID = gl_GlobalInvocationID.x << 5;
#else
    uint threadID = gl_GlobalInvocationID.x;
#endif

    if (threadID < cnt) {
        uint nodeID = threadID + cnt;
#ifdef LEB_REDUCTION_PREPASS
        uint alignedBitOffset = leb__DataBitID(nodeID, u_PassID);
        uint bitField = LEB_HEAP[alignedBitOffset >> 5u];
        uint bitData = 0u;

        // 2-bits
        bitField = (bitField & 0x55555555u) + ((bitField >> 1u) & 0x55555555u);
        bitData = bitField;
        LEB_HEAP[(alignedBitOffset - cnt) >> 5u] = bitData;

        // 3-bits
        bitField = (bitField & 0x33333333u) + ((bitField >>  2u) & 0x33333333u);
        bitData = ((bitField >> 0u) & (7u <<  0u))
                | ((bitField >> 1u) & (7u <<  3u))
                | ((bitField >> 2u) & (7u <<  6u))
                | ((bitField >> 3u) & (7u <<  9u))
                | ((bitField >> 4u) & (7u << 12u))
                | ((bitField >> 5u) & (7u << 15u))
                | ((bitField >> 6u) & (7u << 18u))
                | ((bitField >> 7u) & (7u << 21u));
        leb__SetDataExplicit(nodeID >> 2u, u_PassID - 2, 24u, bitData);

        // 4-bits
        bitField = (bitField & 0x0F0F0F0Fu) + ((bitField >>  4u) & 0x0F0F0F0Fu);
        bitData = ((bitField >>  0u) & (15u <<  0u))
                | ((bitField >>  4u) & (15u <<  4u))
                | ((bitField >>  8u) & (15u <<  8u))
                | ((bitField >> 12u) & (15u << 12u));
        leb__SetDataExplicit(nodeID >> 3u, u_PassID - 3, 16u, bitData);

        // 5-bits
        bitField = (bitField & 0x00FF00FFu) + ((bitField >>  8u) & 0x00FF00FFu);
        bitData = ((bitField >>  0u) & (31u << 0u))
                | ((bitField >> 11u) & (31u << 5u));
        leb__SetDataExplicit(nodeID >> 4u, u_PassID - 4, 10u, bitData);

        // 6-bits
        bitField = (bitField & 0x0000FFFFu) + ((bitField >> 16u) & 0x0000FFFFu);
        bitData = bitField;
        leb__SetDataExplicit(nodeID >> 5u, u_PassID - 5,  6u, bitData);
#else
        uint bitCount = LEB_MAX_DEPTH - u_PassID;
        uint x0 = leb__GetDataExplicit(nodeID << 1u     , u_PassID + 1, bitCount);
        uint x1 = leb__GetDataExplicit(nodeID << 1u | 1u, u_PassID + 1, bitCount);

        leb__SetDataExplicit(nodeID, u_PassID, bitCount + 1u, x0 + x1);
#endif
    }
}
#endif
