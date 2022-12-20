uniform int u_PageResolution;
uniform sampler2DArray u_LeafPages;

#ifdef COMPUTE_SHADER
layout (local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
layout (binding = IMAGE_BINDING, IMAGE_FORMAT) uniform image2D u_Page;

void main()
{
    ivec2 P = ivec2(gl_GlobalInvocationID.xy);
    vec2 Pf = vec2(P) / float(u_PageResolution);

    if (P.x >= u_TextureResolution || P.y >= u_TextureResolution)
        return;

    imageStore(u_Page, P, vec4(1, 0, 0, 0));
}
#endif
