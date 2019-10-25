/* render.glsl - public domain
by Jonathan Dupuy

    This code has dependencies on the following GLSL sources:
    - TerrainRenderCommon.glsl
    - LongestEdgeBisection.glsl
*/


/*******************************************************************************
 * Vertex Shader
 *
 * The vertex shader is empty
 */
#ifdef VERTEX_SHADER
void main()
{ }
#endif

/*******************************************************************************
 * Geometry Shader
 *
 * This tessellaction control shader is responsible for updating the
 * subdivision buffer and sending geometry to the rasterizer.
 */
#ifdef GEOMETRY_SHADER
layout(points) in;
layout(triangle_strip, max_vertices = MAX_VERTICES) out;

layout(location = 0) out vec2 o_TexCoord;
layout(location = 1) out vec2 o_Slope;

vec2[3] DecodeTriangleTexCoords(in const leb_Node node)
{
    mat3 xf = mat3(1.0f);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        xf = leb__SplitMatrix3x3(leb__GetBitValue(node.id, bitID)) * xf;
    }

    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = xf * mat2x3(xPos, yPos);
    vec2 p1 = vec2(pos[0][0], pos[1][0]);
    vec2 p2 = vec2(pos[0][1], pos[1][1]);
    vec2 p3 = vec2(pos[0][2], pos[1][2]);

    return vec2[3](p1, p2, p3);
}

void
GenerateVertex(
    in const vec4 trianglePositions[3],
    in const vec2 triangleTexCoords[3],
    in vec2 tessCoord
) {
    // compute final vertex attributes
    ClipSpaceAttribute attrib = TessellateClipSpaceTriangle(
        trianglePositions,
        triangleTexCoords,
        tessCoord
    );

    // set varyings and emit vertex
    gl_Position = attrib.position;
    o_TexCoord  = attrib.texCoord;
    EmitVertex();
}

void main()
{
    // get threadID (each triangle is associated to a thread)
    // and extract triangle vertices
    leb_Node node = leb_DecodeNode(gl_PrimitiveIDIn);
    vec4 triangleVertices[3] = DecodeTriangleVertices(node);

    // compute target LoD
    vec2 targetLod = LevelOfDetail(triangleVertices);

    // splitting pass
#if FLAG_SPLIT
    if (targetLod.x > 1.0)
        leb_SplitNodeConforming(node);
#endif

    // merging pass
#if FLAG_MERGE
    if (true) {
        leb_DiamondParent diamond = leb_DecodeDiamondParent(node);
        bool shouldMergeBase = LevelOfDetail(DecodeTriangleVertices(diamond.base)).x < 1.0;
        bool shouldMergeTop = LevelOfDetail(DecodeTriangleVertices(diamond.top)).x < 1.0;

        if (shouldMergeBase && shouldMergeTop)
            leb_MergeNodeConforming(node, diamond);
    }
#endif

#if FLAG_CULL
    if (targetLod.y > 0.0) {
#else
    if (true) {
#endif
        // set triangle attributes
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

        // reverse triangle winding depending on the subdivision level
        if ((node.depth & 1) == 0) {
            vec4 tmp1 = trianglePositions[0];
            vec2 tmp2 = triangleTexCoords[0];

            trianglePositions[0] = trianglePositions[2];
            trianglePositions[2] = tmp1;
            triangleTexCoords[0] = triangleTexCoords[2];
            triangleTexCoords[2] = tmp2;
        }

        /*
            The code below generates a tessellated triangle with a single triangle strip.
            The algorithm instances strips of 4 vertices, which produces 2 triangles.
            This is why there is a special case for subd_level == 0, where we expect
            only one triangle.
        */
#if TERRAIN_PATCH_SUBD_LEVEL == 0
        GenerateVertex(trianglePositions, triangleTexCoords, vec2(0, 1));
        GenerateVertex(trianglePositions, triangleTexCoords, vec2(0, 0));
        GenerateVertex(trianglePositions, triangleTexCoords, vec2(1, 0));
        EndPrimitive();
#else
        int nodeDepth = 2 * TERRAIN_PATCH_SUBD_LEVEL - 1;
        uint minNodeID = 1u << nodeDepth;
        uint maxNodeID = 2u << nodeDepth;

        for (uint nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
            leb_Node node = leb_Node(nodeID, nodeDepth);
            vec2 tessCoords[3] = DecodeTriangleTexCoords(node);

            GenerateVertex(trianglePositions, triangleTexCoords, tessCoords[2]);
            GenerateVertex(trianglePositions, triangleTexCoords, tessCoords[1]);
            GenerateVertex(trianglePositions, triangleTexCoords, (tessCoords[0] + tessCoords[2]) / 2.0);
            GenerateVertex(trianglePositions, triangleTexCoords, tessCoords[0]);
            EndPrimitive();
        }
#endif
    }
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
}
#endif
