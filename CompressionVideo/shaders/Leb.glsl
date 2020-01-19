/* leb.glsl - public domain Longest Edge Bisection GLSL library
by Jonathan Dupuy

*/

#ifndef LEB_BUFFER_COUNT
#   define LEB_BUFFER_COUNT    1
#else
#   ifndef BUFFER_BINDING_LEB
#       error User must specify the binding of the LEB buffer
#   endif
#endif
layout(std430, binding = BUFFER_BINDING_LEB)
buffer LebBuffer {
    int minDepth, maxDepth;
    uint heap[];
} u_LebBuffers[LEB_BUFFER_COUNT];

// data structures
struct leb_Node {
    uint id;    // binary code
    int depth;  // subdivision depth
};
struct leb_SameDepthNeighborIDs {
    uint left, right, edge, _reserved;
};
struct leb_DiamondParent {
    leb_Node base, top;
};
struct leb_NodeAndNeighbors {
    leb_Node left, right, edge, node;
};

// manipulation
void leb_SplitNodeConforming(const int lebID, in const leb_Node node);
void leb_SplitNodeConforming_Quad(const int lebID, in const leb_Node node);
void leb_MergeNodeConforming     (const int lebID,
                                  in const leb_Node node,
                                  in const leb_DiamondParent diamond);
void leb_MergeNodeConforming_Quad(const int lebID,
                                  in const leb_Node node,
                                  in const leb_DiamondParent diamond);

// O(1) queries
int leb_MinDepth(const int lebID);
int leb_MaxDepth(const int lebID);
uint leb_NodeCount(const int lebID);
bool leb_IsLeafNode(const int lebID, in const leb_Node node);
bool leb_IsCeilNode(const int lebID, in const leb_Node node);
bool leb_IsRootNode(const int lebID, in const leb_Node node);
bool leb_IsNullNode(                 in const leb_Node node);
leb_Node leb_ParentNode(in const leb_Node node);
leb_SameDepthNeighborIDs leb_GetSameDepthNeighborIDs(in const leb_NodeAndNeighbors nodes);

// O(depth) queries
uint                     leb_EncodeNode(const int lebID, in const leb_Node node);
leb_Node                 leb_DecodeNode(const int lebID, uint bitID);
leb_NodeAndNeighbors     leb_DecodeNodeAndNeighbors     (const int lebID, uint bitID);
leb_NodeAndNeighbors     leb_DecodeNodeAndNeighbors_Quad(const int lebID, uint bitID);
leb_SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs     (in const leb_Node node);
leb_SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs_Quad(in const leb_Node node);
leb_DiamondParent        leb_DecodeDiamondParent     (in const leb_Node node);
leb_DiamondParent        leb_DecodeDiamondParent_Quad(in const leb_Node node);

// intersection test O(depth)
leb_Node leb_BoundingNode     (const int lebID, vec2 p, out vec2 u);
leb_Node leb_BoundingNode_Quad(const int lebID, vec2 p, out vec2 u);
leb_NodeAndNeighbors leb_BoundingNodeAndNeighbors     (const int lebID,
                                                       vec2 p,
                                                       out vec2 u);
leb_NodeAndNeighbors leb_BoundingNodeAndNeighbors_Quad(const int lebID,
                                                       vec2 p,
                                                       out vec2 u);

// subdivision routine O(depth)
vec3   leb_DecodeNodeAttributeArray     (in const leb_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray     (in const leb_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray     (in const leb_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray     (in const leb_Node node, in const mat4x3 data);
vec3   leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat4x3 data);


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------


/*******************************************************************************
 * GetBitValue -- Returns the value of a bit stored in a 32-bit word
 *
 */
uint leb__GetBitValue(uint bitField, uint bitID)
{
    return ((bitField >> bitID) & 1u);
}


/*******************************************************************************
 * SetBitValue -- Sets the value of a bit stored in a 32-bit word
 *
 */
void
leb__SetBitValue(const int lebID, uint heapIndex, uint bitID, uint bitValue)
{
    const uint bitMask = ~(1u << bitID);

    atomicAnd(u_LebBuffers[lebID].heap[heapIndex], bitMask);
    atomicOr(u_LebBuffers[lebID].heap[heapIndex], bitValue << bitID);
}


/*******************************************************************************
 * BitFieldInsert -- Returns the bit field after insertion of some bit data in range
 * [bitOffset, bitOffset + bitCount - 1]
 *
 */
void
leb__BitFieldInsert(
    const int lebID,
    uint bufferIndex,
    uint bitOffset,
    uint bitCount,
    uint bitData
) {
    uint bitMask = ~(~(0xFFFFFFFFu << bitCount) << bitOffset);

    atomicAnd(u_LebBuffers[lebID].heap[bufferIndex], bitMask);
    atomicOr(u_LebBuffers[lebID].heap[bufferIndex], bitData << bitOffset);
}


/*******************************************************************************
 * BitFieldExtract -- Extracts bits [bitOffset, bitOffset + bitCount - 1] from
 * a bit field, returning them in the least significant bits of the result.
 *
 */
uint leb__BitFieldExtract(uint bitField, uint bitOffset, uint bitCount)
{
    uint bitMask = ~(0xFFFFFFFFu << bitCount);

    return (bitField >> bitOffset) & bitMask;
}


/*******************************************************************************
 * IsCeilNode -- Checks if a node is a ceil node, i.e., that can not split further
 *
 */
bool leb_IsCeilNode(const int lebID, in const leb_Node node)
{
    return (node.depth == leb_MaxDepth(lebID));
}


/*******************************************************************************
 * IsRootNode -- Checks if a node is a root node
 *
 */
bool leb_IsRootNode(const int lebID, in const leb_Node node)
{
    return (node.depth == leb_MinDepth(lebID));
}


/*******************************************************************************
 * IsNullNode -- Checks if a node is a null node
 *
 */
bool leb_IsNullNode(in const leb_Node node)
{
    return (node.id == uint(node.depth) /* == 0*/);
}


/*******************************************************************************
 * ParentNode -- Computes the parent of the input node
 *
 */
leb_Node leb__ParentNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id >> 1u, node.depth - 1);
}

leb_Node leb_ParentNode(in const leb_Node node)
{
     return leb_IsNullNode(node) ? node : leb__ParentNode_Fast(node);
}


/*******************************************************************************
 * CeilNode -- Returns the associated ceil node, i.e., the deepest possible leaf
 *
 */
leb_Node leb__CeilNode_Fast(const int lebID, in const leb_Node node)
{
    int maxDepth = leb_MaxDepth(lebID);
    return leb_Node(node.id << (maxDepth - node.depth), maxDepth);
}

leb_Node leb__CeilNode(const int lebID, in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__CeilNode_Fast(lebID, node);
}


/*******************************************************************************
 * SiblingNode -- Computes the sibling of the input node
 *
 */
leb_Node leb__SiblingNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id ^ 1u, node.depth);
}

leb_Node leb__SiblingNode(in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__SiblingNode_Fast(node);
}


/*******************************************************************************
 * RightSiblingNode -- Computes the right sibling of the input node
 *
 */
leb_Node leb__RightSiblingNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id | 1u, node.depth);
}

leb_Node leb__RightSiblingNode(in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__RightSiblingNode_Fast(node);
}


/*******************************************************************************
 * LeftSiblingNode -- Computes the left sibling of the input node
 *
 */
leb_Node leb__LeftSiblingNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id & (~1u), node.depth);
}

leb_Node leb__LeftSiblingNode(in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__LeftSiblingNode_Fast(node);
}


/*******************************************************************************
 * RightChildNode -- Computes the right child of the input node
 *
 */
leb_Node leb__RightChildNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id << 1u | 1u, node.depth + 1);
}

leb_Node leb__RightChildNode(in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__RightChildNode_Fast(node);
}


/*******************************************************************************
 * LeftChildNode -- Computes the left child of the input node
 *
 */
leb_Node leb__LeftChildNode_Fast(in const leb_Node node)
{
    return leb_Node(node.id << 1u, node.depth + 1);
}

leb_Node leb__LeftChildNode(in const leb_Node node)
{
    return leb_IsNullNode(node) ? node : leb__LeftChildNode_Fast(node);
}


/*******************************************************************************
 * HeapBitSize -- Computes the number of bits to allocate for the buffer
 *
 * For a tree of max depth D, the number of bits is 2^(D+2).
 * Note that 2 bits are "wasted" in the sense that they only serve
 * to round the required number of bits to a power of two.
 *
 */
uint leb__HeapBitSize(uint lebMaxDepth)
{
    return 1u << (lebMaxDepth + 2u);
}


/*******************************************************************************
 * HeapUint32Size -- Computes the number of uints to allocate for the bitfield
 *
 */
uint leb__HeapUint32Size(uint lebMaxDepth)
{
    return leb__HeapBitSize(lebMaxDepth) >> 5u;
}


/*******************************************************************************
 * NodeBitID -- Returns the bit index that stores data associated with a given node
 *
 * For a LEB of max depth D and given an index in [0, 2^(D+1) - 1], this
 * functions is used to emulate the behaviour of a lookup in an array, i.e.,
 * uint32_t[nodeID]. It provides the first bit in memory that stores
 * information associated with the element of index nodeID.
 *
 * For data located at level d, the bit offset is 2^d x (3 - d + D)
 * We then offset this quantity by the index by (nodeID - 2^d) x (D + 1 - d)
 * Note that the null index (nodeID = 0) is also supported.
 *
 */
uint leb__NodeBitID(const int lebID, in const leb_Node node)
{
    uint tmp1 = 2u << node.depth;
    uint tmp2 = uint(1 + leb_MaxDepth(lebID) - node.depth);

    return tmp1 + node.id * tmp2;
}


/*******************************************************************************
 * NodeBitID_BitField -- Computes the bitfield bit location associated to a node
 *
 * Here, the node is converted into a final node and its bit offset is
 * returned, which is finalNodeID + 2^{D + 1}
 */
uint leb__NodeBitID_BitField(const int lebID, in const leb_Node node)
{
    return leb__NodeBitID(lebID, leb__CeilNode(lebID, node));
}


/*******************************************************************************
 * DataBitSize -- Returns the number of bits associated with a given node
 *
 */
int leb__NodeBitSize(const int lebID, in const leb_Node node)
{
    return leb_MaxDepth(lebID) - node.depth + 1;
}


/*******************************************************************************
 * HeapArgs
 *
 * The LEB heap data structure uses an array of 32-bit words to store its data.
 * Whenever we need to access a certain bit range, we need to query two such
 * words (because sometimes the requested bit range overlaps two 32-bit words).
 * The HeapArg data structure provides arguments for reading from and/or
 * writing to the two 32-bit words that bound the queries range.
 *
 */
struct leb__HeapArgs {
    uint heapIndexLSB, heapIndexMSB;
    uint bitOffsetLSB;
    uint bitCountLSB, bitCountMSB;
};

leb__HeapArgs
leb__CreateHeapArgs(const int lebID, in const leb_Node node, int bitCount)
{
    uint alignedBitOffset = leb__NodeBitID(lebID, node);
    uint maxHeapIndex = leb__HeapUint32Size(leb_MaxDepth(lebID)) - 1u;
    uint heapIndexLSB = (alignedBitOffset >> 5u);
    uint heapIndexMSB = min(heapIndexLSB + 1, maxHeapIndex);
    leb__HeapArgs args;

    args.bitOffsetLSB = alignedBitOffset & 31u;
    args.bitCountLSB = min(32u - args.bitOffsetLSB, bitCount);
    args.bitCountMSB = bitCount - args.bitCountLSB;
    args.heapIndexLSB = heapIndexLSB;
    args.heapIndexMSB = heapIndexMSB;

    return args;
}


/*******************************************************************************
 * HeapWrite -- Sets bitCount bits located at nodeID to bitData
 *
 * Note that this procedure writes to at most two uint32 elements.
 * Two elements are relevant whenever the specified interval overflows 32-bit
 * words.
 *
 */
void
leb__HeapWriteExplicit(
    const int lebID,
    in const leb_Node node,
    int bitCount,
    uint bitData
) {
    leb__HeapArgs args = leb__CreateHeapArgs(lebID, node, bitCount);

    leb__BitFieldInsert(lebID,
                        args.heapIndexLSB,
                        args.bitOffsetLSB,
                        args.bitCountLSB,
                        bitData);
    leb__BitFieldInsert(lebID,
                        args.heapIndexMSB,
                        0u,
                        args.bitCountMSB,
                        bitData >> args.bitCountLSB);
}

void leb__HeapWrite(const int lebID, in const leb_Node node, uint bitData)
{
    leb__HeapWriteExplicit(lebID, node, leb__NodeBitSize(lebID, node), bitData);
}


/*******************************************************************************
 * HeapRead -- Returns bitCount bits located at nodeID
 *
 * Note that this procedure writes to at most two uint32 elements.
 * Two elements are relevant whenever the specified interval overflows 32-bit
 * words.
 *
 */
uint
leb__HeapReadExplicit(const int lebID, in const leb_Node node, int bitCount)
{
    leb__HeapArgs args = leb__CreateHeapArgs(lebID, node, bitCount);
    uint lsb = leb__BitFieldExtract(u_LebBuffers[lebID].heap[args.heapIndexLSB],
                                    args.bitOffsetLSB,
                                    args.bitCountLSB);
    uint msb = leb__BitFieldExtract(u_LebBuffers[lebID].heap[args.heapIndexMSB],
                                    0u,
                                    args.bitCountMSB);

    return (lsb | (msb << args.bitCountLSB));
}

uint leb__HeapRead(const int lebID, in const leb_Node node)
{
    return leb__HeapReadExplicit(lebID, node, leb__NodeBitSize(lebID, node));
}


/*******************************************************************************
 * HeapWrite_BitField -- Sets the bit associated to a leaf node to bitValue
 *
 * This is a dedicated routine to write directly to the bitfield.
 *
 */
void
leb__HeapWrite_BitField(const int lebID, in const leb_Node node, uint bitValue)
{
    uint bitID = leb__NodeBitID_BitField(lebID, node);

    leb__SetBitValue(lebID, bitID >> 5u, bitID & 31u, bitValue);
}


/*******************************************************************************
 * HeapRead_BitField -- Returns the value of the bit associated to a leaf node
 *
 * This is a dedicated routine to read directly from the bitfield.
 *
 */
uint leb__HeapRead_BitField(const int lebID, in const leb_Node node)
{
    uint bitID = leb__NodeBitID_BitField(lebID, node);

    return leb__GetBitValue(u_LebBuffers[lebID].heap[bitID >> 5u], bitID & 31u);
}


/*******************************************************************************
 * IsLeafNode -- Checks if a node is a leaf node
 *
 */
bool leb_IsLeafNode(const int lebID, in const leb_Node node)
{
    return (leb__HeapRead(lebID, node) == 1u);
}


/*******************************************************************************
 * Split -- Subdivides a node in two
 *
 */
void leb__SplitNode(const int lebID, in const leb_Node node)
{
    leb__HeapWrite_BitField(lebID, leb__RightChildNode(node), 1u);
}


/*******************************************************************************
 * Merge -- Merges the node with its neighbour
 *
 */
void leb__MergeNode(const int lebID, in const leb_Node node)
{
    leb__HeapWrite_BitField(lebID, leb__RightSiblingNode(node), 0u);
}


/*******************************************************************************
 * MinDepth -- Returns the minimum LEB depth
 *
 */
int leb_MinDepth(const int lebID)
{
    return u_LebBuffers[lebID].minDepth;
}


/*******************************************************************************
 * MinDepth -- Returns the minimum LEB depth
 *
 */
int leb_MaxDepth(const int lebID)
{
    return u_LebBuffers[lebID].maxDepth;
}


/*******************************************************************************
 * NodeCount -- Returns the number of triangles in the LEB
 *
 */
uint leb_NodeCount(const int lebID)
{
    return leb__HeapRead(lebID, leb_Node(1u, 0));
}


/*******************************************************************************
 * Decode the LEB Node associated to an index
 *
 */
leb_Node leb_DecodeNode(const int lebID, uint nodeID)
{
    leb_Node node = leb_Node(1u, 0);

    while (leb__HeapRead(lebID, node) > 1u) {
        leb_Node leftChild = leb__LeftChildNode(node);
        uint cmp = leb__HeapRead(lebID, leftChild);
        uint b = nodeID < cmp ? 0 : 1;

        node = leftChild;
        node.id|= b;
        nodeID-= cmp * b;
    }

    return node;
}


/*******************************************************************************
 * EncodeNode -- Returns the bit index associated with the Node
 *
 * This does the inverse of the DecodeNode routine.
 *
 */
uint leb_EncodeNode(const int lebID, in const leb_Node node)
{
    uint nodeID = 0u;
    leb_Node nodeIterator = node;

    while (nodeIterator.id > 1u) {
        leb_Node sibling = leb__LeftSiblingNode(nodeIterator);
        uint nodeCount = leb__HeapRead(lebID, sibling);

        nodeID+= (nodeIterator.id & 1u) * nodeCount;
        nodeIterator = leb_ParentNode(nodeIterator);
    }

    return nodeID;
}


/*******************************************************************************
 * SplitNodeIDs -- Updates the IDs of neighbors after one LEB split
 *
 * This code applies the following rules:
 * Split left:
 * LeftID  = 2 * NodeID + 1
 * RightID = 2 * EdgeID + 1
 * EdgeID  = 2 * RightID + 1
 *
 * Split right:
 * LeftID  = 2 * EdgeID
 * RightID = 2 * NodeID
 * EdgeID  = 2 * LeftID
 *
 * The _reserved channel stores NodeID, which is recquired for applying the
 * rules.
 *
 */
leb_SameDepthNeighborIDs
leb__SplitNodeIDs(in const leb_SameDepthNeighborIDs nodeIDs, uint splitBit)
{
#if 1 // branchless version
    uint b = splitBit;
    uint c = splitBit ^ 1u;
    bool cb = bool(c);
    uvec4 idArray = uvec4(nodeIDs.left, nodeIDs.right, nodeIDs.edge, nodeIDs._reserved);
    leb_SameDepthNeighborIDs newIDs = {
        (idArray[2 + b] << 1u) | uint(cb && bool(idArray[2 + b])),
        (idArray[2 + c] << 1u) | uint(cb && bool(idArray[2 + c])),
        (idArray[b    ] << 1u) | uint(cb && bool(idArray[b    ])),
        (idArray[3    ] << 1u) | b
    };

    return newIDs;
#else
    uint n1 = nodeIDs.left, n2 = nodeIDs.right,
         n3 = nodeIDs.edge, n4 = nodeIDs._reserved;
    uint b2 = (n2 == 0u) ? 0u : 1u,
         b3 = (n3 == 0u) ? 0u : 1u;

    if (splitBit == 0u) {
        return leb_SameDepthNeighborIDs(
            n4 << 1 | 1, n3 << 1 | b3, n2 << 1 | b2, n4 << 1
        );
    } else {
        return leb_SameDepthNeighborIDs(
            n3 << 1    , n4 << 1     , n1 << 1     , n4 << 1 | 1
        );
    }
#endif
}


/*******************************************************************************
 * DecodeNodeNeighborIDs -- Decodes the IDs of the leb_Nodes neighbor to node
 *
 * The IDs are associated to the depth of the input node. As such, they
 * don't necessarily exist in the LEB subdivision.
 *
 */
leb_SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs(in const leb_Node node)
{
    leb_SameDepthNeighborIDs nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 0u, 1u);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}

leb_SameDepthNeighborIDs
leb_DecodeSameDepthNeighborIDs_Quad(in const leb_Node node)
{
    if (node.depth == 0)
        return leb_SameDepthNeighborIDs(0u, 0u, 0u, 1u);

    uint b = leb__GetBitValue(node.id, node.depth - 1);
    leb_SameDepthNeighborIDs nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 3u - b, 2u + b);

    for (int bitID = node.depth - 2; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}


/*******************************************************************************
 * SameDepthNeighborIDs -- Computes the IDs of the same-level neighbors of a node
 *
 */
leb_SameDepthNeighborIDs
leb_GetSameDepthNeighborIDs(in const leb_NodeAndNeighbors nodes)
{
    uint edgeID = nodes.edge.id << (nodes.node.depth - nodes.edge.depth);
    uint leftID = nodes.left.id >> (nodes.left.depth - nodes.node.depth);
    uint rightID = nodes.right.id >> (nodes.right.depth - nodes.node.depth);

    return leb_SameDepthNeighborIDs(leftID, rightID, edgeID, nodes.node.id);
}


/*******************************************************************************
 * EdgeNode -- Computes the neighbour of the input node wrt to its longest edge
 *
 */
leb_Node leb__EdgeNode(in const leb_Node node)
{
    uint nodeID = leb_DecodeSameDepthNeighborIDs(node).edge;

    return leb_Node(nodeID, (nodeID == 0u) ? 0 : node.depth);
}

leb_Node leb__EdgeNode_Quad(in const leb_Node node)
{
    uint nodeID = leb_DecodeSameDepthNeighborIDs_Quad(node).edge;

    return leb_Node(nodeID, (nodeID == 0u) ? 0 : node.depth);
}


/*******************************************************************************
 * SplitNodeConforming -- Splits a node while producing a conforming LEB
 *
 */
void leb_SplitNodeConforming(const int lebID, in const leb_Node node)
{
    if (!leb_IsCeilNode(lebID, node)) {
        const uint minNodeID = 1u << leb_MinDepth(lebID);
        leb_Node nodeIterator = node;

        leb__SplitNode(lebID, nodeIterator);
        nodeIterator = leb__EdgeNode(nodeIterator);

        while (nodeIterator.id >= minNodeID) {
            leb__SplitNode(lebID, nodeIterator);
            nodeIterator = leb_ParentNode(nodeIterator);
            leb__SplitNode(lebID, nodeIterator);
            nodeIterator = leb__EdgeNode(nodeIterator);
        }
    }
}

void leb_SplitNodeConforming_Quad(const int lebID, in const leb_Node node)
{
    if (!leb_IsCeilNode(lebID, node)) {
        const uint minNodeID = 1u << leb_MinDepth(lebID);
        leb_Node nodeIterator = node;

        leb__SplitNode(lebID, nodeIterator);
        nodeIterator = leb__EdgeNode_Quad(nodeIterator);

        while (nodeIterator.id >= minNodeID) {
            leb__SplitNode(lebID, nodeIterator);
            nodeIterator = leb_ParentNode(nodeIterator);
            leb__SplitNode(lebID, nodeIterator);
            nodeIterator = leb__EdgeNode_Quad(nodeIterator);
        }
    }
}


/*******************************************************************************
 * MergeNodeConforming -- Merges a node while producing a conforming LEB
 *
 * This routines makes sure that the children of a diamond (including the
 * input node) all exist in the LEB before calling a merge.
 *
 */
void
leb_MergeNodeConforming(
    const int lebID,
    in const leb_Node node,
    in const leb_DiamondParent diamond
) {
    if (!leb_IsRootNode(lebID, node)) {
        leb_Node dualNode = leb__RightChildNode(diamond.top);
        bool b1 = leb_IsLeafNode(lebID, leb__SiblingNode_Fast(node));
        bool b2 = leb_IsLeafNode(lebID, dualNode);
        bool b3 = leb_IsLeafNode(lebID, leb__SiblingNode(dualNode));

        if (b1 && b2 && b3) {
            leb__MergeNode(lebID, node);
            leb__MergeNode(lebID, dualNode);
        }
    }
}

void
leb_MergeNodeConforming_Quad(
    const int lebID,
    in const leb_Node node,
    in const leb_DiamondParent diamond
) {
    leb_MergeNodeConforming(lebID, node, diamond);
}


/*******************************************************************************
 * DecodeNodeDiamondIDs -- Decodes the upper Diamond associated to the leb_Node
 *
 * If the neighbour part does not exist, the parentNode is copied instead.
 *
 */
leb_DiamondParent leb_DecodeDiamondParent(in const leb_Node node)
{
    leb_Node parentNode = leb_ParentNode(node);
    uint diamondNodeID = leb_DecodeSameDepthNeighborIDs(parentNode).edge;
    leb_Node diamondNode = leb_Node(
        diamondNodeID > 0u ? diamondNodeID : parentNode.id,
        parentNode.depth
    );

    return leb_DiamondParent(parentNode, diamondNode);
}

leb_DiamondParent leb_DecodeDiamondParent_Quad(in const leb_Node node)
{
    leb_Node parentNode = leb_ParentNode(node);
    uint diamondNodeID = leb_DecodeSameDepthNeighborIDs_Quad(parentNode).edge;
    leb_Node diamondNode = leb_Node(
        diamondNodeID > 0u ? diamondNodeID : parentNode.id,
        parentNode.depth
    );

    return leb_DiamondParent(parentNode, diamondNode);
}


/*******************************************************************************
 * NodeAndNeighborsFromSameDepthNeighborIDs -- Decodes the true neighbors of a node
 *
 */
leb_Node leb_NodeCtor(uint id, int depth)
{
    return id == 0 ? leb_Node(0, 0) : leb_Node(id, depth);
}

leb_NodeAndNeighbors
leb__NodeAndNeighborsFromSameDepthNeighborIDs(
    const int lebID,
    in const leb_SameDepthNeighborIDs nodeIDs,
    int nodeDepth
) {
    leb_NodeAndNeighbors nodeData = leb_NodeAndNeighbors(
        leb_NodeCtor(nodeIDs.left, nodeDepth),
        leb_NodeCtor(nodeIDs.right, nodeDepth),
        leb_NodeCtor(nodeIDs.edge, nodeDepth),
        leb_NodeCtor(nodeIDs._reserved, nodeDepth)
    );

    if (!leb_IsLeafNode(lebID, nodeData.edge))
        nodeData.edge = leb_ParentNode(nodeData.edge);

    if (!leb_IsLeafNode(lebID, nodeData.left))
        nodeData.left = leb__RightChildNode(nodeData.left);

    if (!leb_IsLeafNode(lebID, nodeData.right))
        nodeData.right = leb__LeftChildNode(nodeData.right);

    return nodeData;
}


/*******************************************************************************
 * DecodeNodeAndNeighbors -- Decode the LEB Node associated to an index, along with its neighbors
 *
 */
leb_NodeAndNeighbors
leb_DecodeNodeAndNeighbors(const int lebID, uint threadID)
{
#define nodeID nodeIDs._reserved
    leb_SameDepthNeighborIDs nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 0u, 1u);
    int nodeDepth = 0;

    while (leb__HeapRead(lebID, leb_Node(nodeID, nodeDepth)) > 1u) {
        leb_Node leftChildNode = leb__LeftChildNode(leb_Node(nodeID, nodeDepth));
        uint cmp = leb__HeapRead(lebID, leftChildNode);
        uint b = threadID < cmp ? 0u : 1u;

        nodeIDs = leb__SplitNodeIDs(nodeIDs, b);
        threadID-= cmp * b;
    }

    return leb__NodeAndNeighborsFromSameDepthNeighborIDs(lebID, nodeIDs, nodeDepth);
#undef nodeID
}

leb_NodeAndNeighbors
leb_DecodeNodeAndNeighbors_Quad(const int lebID, uint threadID)
{
#define nodeID nodeIDs._reserved
    leb_SameDepthNeighborIDs nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 0u, 1u);
    int nodeDepth = 0;

    while (leb__HeapRead(lebID, leb_Node(nodeID, nodeDepth)) > 1u) {
        leb_Node leftChildNode = leb__LeftChildNode(leb_Node(nodeID, nodeDepth));
        uint cmp = leb__HeapRead(lebID, leftChildNode);
        uint b = threadID < cmp ? 0u : 1u;

        nodeIDs = leb__SplitNodeIDs(nodeIDs, b);
        threadID-= cmp * b;
    }

    return leb__NodeAndNeighborsFromSameDepthNeighborIDs(lebID, nodeIDs, nodeDepth);
#undef nodeID
}


/*******************************************************************************
 * SplitMatrix3x3 -- Computes a LEB splitting matrix from a split bit
 *
 */
mat3 leb__SplittingMatrix(uint splitBit)
{
    float b = float(splitBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c   , b   , 0.0f,
        0.5f, 0.0f, 0.5f,
        0.0f,    c,    b
    ));
}


/*******************************************************************************
 * QuadMatrix3x3 -- Computes the matrix that affects the triangle to the quad
 *
 */
mat3 leb__QuadMatrix(uint quadBit)
{
    float b = float(quadBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c, 0.0f, b,
        b, c   , b,
        b, 0.0f, c
    ));
}


/*******************************************************************************
 * DecodeTransformationMatrix -- Computes the splitting matrix associated to a LEB
 * node
 *
 */
mat3 leb__DecodeTransformationMatrix(in const leb_Node node)
{
    mat3 xf = mat3(1.0f);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    return xf;
}

mat3 leb__DecodeTransformationMatrix_Quad(in const leb_Node node)
{
    mat3 xf = leb__QuadMatrix(leb__GetBitValue(node.id, node.depth - 1));

    for (int bitID = node.depth - 2; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    return xf;
}


/*******************************************************************************
 * DecodeNodeAttributeArray -- Compute the triangle attributes at the input node
 *
 */
vec3 leb_DecodeNodeAttributeArray(in const leb_Node node, in const vec3 data)
{
    return leb__DecodeTransformationMatrix(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray(in const leb_Node node, in const mat2x3 data)
{
    return leb__DecodeTransformationMatrix(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray(in const leb_Node node, in const mat3x3 data)
{
    return leb__DecodeTransformationMatrix(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray(in const leb_Node node, in const mat4x3 data)
{
    return leb__DecodeTransformationMatrix(node) * data;
}

vec3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const vec3 data)
{
    return leb__DecodeTransformationMatrix_Quad(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat2x3 data)
{
    return leb__DecodeTransformationMatrix_Quad(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat3x3 data)
{
    return leb__DecodeTransformationMatrix_Quad(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray_Quad(in const leb_Node node, in const mat4x3 data)
{
    return leb__DecodeTransformationMatrix_Quad(node) * data;
}


/*******************************************************************************
 * BoundingNode -- Compute the triangle that bounds the point (x, y)
 *
 */
leb_Node leb_BoundingNode(const int lebID, vec2 p, out vec2 u)
{
    leb_Node node = leb_Node(0u, 0);

    if (p.x >= 0.0f && p.y >= 0.0f && p.x + p.y <= 1.0f) {
        node = leb_Node(1u, 0);

        while (!leb_IsLeafNode(lebID, node) && !leb_IsCeilNode(lebID, node)) {
            vec2 q = p;

            if (q.x < q.y) {
                node = leb__LeftChildNode(node);
                p.x = (1.0f - q.x - q.y);
                p.y = (q.y - q.x);
            } else {
                node = leb__RightChildNode(node);
                p.x = (q.x - q.y);
                p.y = (1.0f - q.x - q.y);
            }
        }

        u = p;
    }

    return node;
}

leb_Node leb_BoundingNode_Quad(const int lebID, vec2 p, out vec2 u)
{
    leb_Node node = leb_Node(0u, 0);

    if (p.x >= 0.0f && p.y >= 0.0f && p.x <= 1.0f && p.y <= 1.0f) {
        if (p.x + p.y <= 1.0f) {
            node = leb_Node(2u, 1);
        } else {
            node = leb_Node(3u, 1);
            p.x = 1.0f - p.x;
            p.y = 1.0f - p.y;
        }

        while (!leb_IsLeafNode(lebID, node) && !leb_IsCeilNode(lebID, node)) {
            vec2 q = p;

            if (q.x < q.y) {
                node = leb__LeftChildNode(node);
                p.x = (1.0f - q.x - q.y);
                p.y = (q.y - q.x);
            } else {
                node = leb__RightChildNode(node);
                p.x = (q.x - q.y);
                p.y = (1.0f - q.x - q.y);
            }
        }

        u = p;
    }

    return node;
}


/*******************************************************************************
 * BoundingNodeAndNeighbors -- Compute the triangle that bounds the point (x, y)
 *
 */
#if 0
leb_NodeAndNeighbors
leb_BoundingNodeAndNeighbors(const int lebID, vec2 p, out vec2 u)
{
    leb_Node node = leb_Node(0u, 0);

    if (p.x >= 0.0f && p.y >= 0.0f && p.x + p.y <= 1.0f) {
        node = leb_Node(1u, 0);

        while (!leb_IsLeafNode(lebID, node) && !leb_IsCeilNode(lebID, node)) {
            vec2 q = p;

            if (q.x < q.y) {
                node = leb__LeftChildNode(node);
                p.x = (1.0f - q.x - q.y);
                p.y = (q.y - q.x);
            } else {
                node = leb__RightChildNode(node);
                p.x = (q.x - q.y);
                p.y = (1.0f - q.x - q.y);
            }
        }

        u = p;
    }

    return node;
}
#endif

leb_NodeAndNeighbors
leb_BoundingNodeAndNeighbors_Quad(const int lebID, vec2 p, out vec2 u)
{
    leb_SameDepthNeighborIDs nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 0u, 1u);
    int nodeDepth = 0;

    if (p.x >= 0.0f && p.y >= 0.0f && p.x <= 1.0f && p.y <= 1.0f) {
        if (p.x + p.y <= 1.0f) {
            nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 3u, 2u);
        } else {
            nodeIDs = leb_SameDepthNeighborIDs(0u, 0u, 2u, 3u);
            p.x = 1.0f - p.x;
            p.y = 1.0f - p.y;
        }

        nodeDepth = 1;
        while (!leb_IsLeafNode(lebID, leb_Node(nodeIDs._reserved, nodeDepth)) &&
               !leb_IsCeilNode(lebID, leb_Node(nodeIDs._reserved, nodeDepth))) {
            vec2 q = p;

            if (q.x < q.y) {
                nodeIDs = leb__SplitNodeIDs(nodeIDs, 0u);
                p.x = (1.0f - q.x - q.y);
                p.y = (q.y - q.x);
            } else {
                nodeIDs = leb__SplitNodeIDs(nodeIDs, 1u);
                p.x = (q.x - q.y);
                p.y = (1.0f - q.x - q.y);
            }

            ++nodeDepth;
        }

        u = p;
    }

    return leb__NodeAndNeighborsFromSameDepthNeighborIDs(lebID, nodeIDs, nodeDepth);
}

