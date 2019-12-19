uniform int u_PassID;
layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

void main(void)
{
    const int lebID = 0;
    uint cnt = (1u << u_PassID);
    uint threadID = gl_GlobalInvocationID.x;

    if (threadID < cnt) {
        uint nodeID = threadID + cnt;
        uint x0 = leb__HeapRead(lebID, leb_Node(nodeID << 1u     , u_PassID + 1));
        uint x1 = leb__HeapRead(lebID, leb_Node(nodeID << 1u | 1u, u_PassID + 1));

        leb__HeapWrite(lebID, leb_Node(nodeID, u_PassID), x0 + x1);
    }
}
