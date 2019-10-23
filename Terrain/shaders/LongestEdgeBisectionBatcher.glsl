// this shader sets the indirect drawing commands
#ifdef COMPUTE_SHADER

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

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    u_DrawArraysIndirectCommand[0] = leb_NodeCount();

#if FLAG_MS
    u_DrawMeshTasksIndirectCommand[0] = max(1, (leb_NodeCount() >> 5) + 1);
#endif

}

#endif
