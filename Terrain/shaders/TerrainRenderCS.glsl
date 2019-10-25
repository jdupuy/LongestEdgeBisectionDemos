/* render.glsl - public domain
by Jonathan Dupuy

    This code has dependencies on the following GLSL sources:
    - TerrainRenderCommon.glsl
    - LongestEdgeBisection.glsl
*/

layout(std430, binding = BUFFER_BINDING_LEB_NODE_BUFFER)
readonly buffer NodeBuffer {
    uint u_NodeBuffer[];
};


/*******************************************************************************
 * Vertex Shader
 *
 * The vertex shader is empty
 */
#ifdef VERTEX_SHADER
layout(location = 0) in vec2 i_VertexPos;
layout(location = 0) out vec2 o_TexCoord;

void main()
{
    uint nodeID = u_NodeBuffer[gl_InstanceID];
    leb_Node node = leb_Node(nodeID, findMSB(nodeID));
    //leb_Node node = leb_Node(gl_InstanceID + 2, 1);
    vec4 triangleVertices[3] = DecodeTriangleVertices(node);
    vec4 trianglePositions[3] = vec4[3](
        u_ModelViewProjectionMatrix * vec4(triangleVertices[0].xy, 0.0f, 1.0f),
        u_ModelViewProjectionMatrix * vec4(triangleVertices[1].xy, 0.0f, 1.0f),
        u_ModelViewProjectionMatrix * vec4(triangleVertices[2].xy, 0.0f, 1.0f)
    );
    vec2 triangleTexCoords[3] = vec2[3](
        triangleVertices[0].xy,
        triangleVertices[1].xy,
        triangleVertices[2].xy
    );

    // change winding depending on node level
    if ((node.depth & 1) == 0) {
        vec4 tmp1 = trianglePositions[0];
        vec2 tmp2 = triangleTexCoords[0];

        trianglePositions[0] = trianglePositions[2];
        trianglePositions[2] = tmp1;
        triangleTexCoords[0] = triangleTexCoords[2];
        triangleTexCoords[2] = tmp2;
    }

    // compute final vertex attributes
    ClipSpaceAttribute attrib = TessellateClipSpaceTriangle(
        trianglePositions,
        triangleTexCoords,
        i_VertexPos
    );

    // set varyings
    gl_Position = attrib.position;
    o_TexCoord  = attrib.texCoord;
}
#endif


/*******************************************************************************
 * Fragment Shader -- Shades the terrain
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = ShadeFragment(i_TexCoord);
    //o_FragColor = texture(u_DmapSampler, i_TexCoord); //vec4(i_TexCoord, 0, 1);
}
#endif
