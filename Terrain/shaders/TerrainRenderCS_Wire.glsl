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
 * Geometry Shader
 *
 * This geometry shader is used properly on the input mesh (here a terrain).
 */
#ifdef GEOMETRY_SHADER
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec2 i_TexCoord[];
layout(location = 0) out vec2 o_TexCoord;
layout(location = 1) noperspective out vec3 o_Distance;

uniform vec2 u_ScreenResolution;

void main()
{
    vec2 p0 = u_ScreenResolution * gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
    vec2 p1 = u_ScreenResolution * gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
    vec2 p2 = u_ScreenResolution * gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    for (int i = 0; i < 3; ++i) {
        o_TexCoord = i_TexCoord[i];
        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }

    EndPrimitive();
}
#endif

/*******************************************************************************
 * Fragment Shader -- Shades the terrain
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 1) noperspective in vec3 i_Distance;
layout(location = 0) out vec4 o_FragColor;

void main()
{
    const float wireScale = 1.0; // scale of the wire in pixel
    vec4 wireColor = vec4(0.0, 0.75, 1.0, 1.0);
    vec3 distanceSquared = i_Distance * i_Distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);

    o_FragColor = mix(ShadeFragment(i_TexCoord), wireColor, blendFactor);
}
#endif
