
uniform mat4 u_ModelViewProjection;
uniform int u_PageTextureID = 0;

#ifdef VERTEX_SHADER
layout (location = 0) out vec2 o_TexCoord;

void main()
{
    vec2 P = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1);

    o_TexCoord = P;
    gl_Position = u_ModelViewProjection * vec4(P, 0.0f, 1.0f);
}
#endif

#ifdef FRAGMENT_SHADER
layout (location = 0) in vec2 i_TexCoord;
layout (location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = tt_texture(u_PageTextureID, i_TexCoord);
    //vec2 Q;
    //vec2 P = i_TexCoord;
    //leb_Node node   = leb_BoundingNode_Quad(0, P, Q);
    //vec3 PP = vec3(i_TexCoord, tt_Indirections[3]);
    //o_FragColor = texture(tt_Textures[0], PP);
    //o_FragColor = vec4(i_TexCoord, 0, 0);
}
#endif
