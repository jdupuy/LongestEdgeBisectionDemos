#ifdef COMPUTE_SHADER
layout(std430, binding = BUFFER_BINDING_DISPATCH_INDIRECT_COMMAND)
buffer DispatchIndirectCommandBuffer {
    uint u_DispatchIndirectCommand[];
};

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    const int lebID = 0;
    uint nodeCount = leb_NodeCount(lebID);

    u_DispatchIndirectCommand[0] = nodeCount / 256u + 1u;
    u_DispatchIndirectCommand[7] = nodeCount;
}
#endif
