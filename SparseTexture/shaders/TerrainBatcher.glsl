// this shader sets the indirect drawing commands
#ifdef COMPUTE_SHADER

uniform int u_LebID = 0;

layout(std430, binding = BUFFER_BINDING_DRAW_ARRAYS_INDIRECT_COMMAND)
buffer DrawArraysIndirectCommandBuffer {
    uint u_DrawArraysIndirectCommand[];
};

#if FLAG_MS
layout(std430, binding = BUFFER_BINDING_DRAW_MESH_TASKS_INDIRECT_COMMAND)
buffer DrawMeshTasksIndirectCommandBuffer {
    uint u_DrawMeshTasksIndirectCommand[];
};
#endif

#if FLAG_CS
layout(std430, binding = BUFFER_BINDING_DRAW_ELEMENTS_INDIRECT_COMMAND)
buffer DrawElementsIndirectCommandBuffer {
    uint u_DrawElementsIndirectCommand[];
};
layout(std430, binding = BUFFER_BINDING_DISPATCH_INDIRECT_COMMAND)
buffer DispatchIndirectCommandBuffer {
    uint u_DispatchIndirectCommand[];
};
layout(binding = BUFFER_BINDING_LEB_NODE_COUNTER)
uniform atomic_uint u_LebNodeCounter;
// This function is implemented to support intel, AMD, and NVidia GPUs.
uint atomicCounterExchangeImpl(atomic_uint c, uint data)
{
#if ATOMIC_COUNTER_EXCHANGE_ARB
    return atomicCounterExchangeARB(c, data);
#elif ATOMIC_COUNTER_EXCHANGE_AMD
    return atomicCounterExchange(c, data);
#else
#error please configure atomicCounterExchange for your platform
#endif
}
#endif

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    const int lebID = u_LebID;
    uint nodeCount = leb_NodeCount(lebID);

    u_DrawArraysIndirectCommand[0] = nodeCount;

#if FLAG_MS
    u_DrawMeshTasksIndirectCommand[0] = max(1, (nodeCount >> 5) + 1);
#endif

#if FLAG_CS
    u_DispatchIndirectCommand[0] = nodeCount / 256u + 1u;
    u_DrawElementsIndirectCommand[0] = MESHLET_INDEX_COUNT;
    u_DrawElementsIndirectCommand[1] = atomicCounter(u_LebNodeCounter);
    atomicCounterExchangeImpl(u_LebNodeCounter, 0u);
#endif
}

#endif
