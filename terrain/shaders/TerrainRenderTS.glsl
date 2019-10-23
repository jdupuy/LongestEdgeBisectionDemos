/* render.glsl - public domain
by Jonathan Dupuy

    This code has dependencies on the following GLSL sources:
    - TerrainRenderCommon.glsl
    - leb.glsl
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
 * Tessellation Control Shader
 *
 * This tessellaction control shader is responsible for updating the
 * subdivision buffer and sending geometry to the rasterizer.
 */
#ifdef TESS_CONTROL_SHADER
layout (vertices = 1) out;
out PatchData {
    vec4 packedData[4];
} o_Patch[];


void main()
{
    // get threadID (each triangle is associated to a thread)
    // and extract triangle vertices
    leb_Node node = leb_DecodeNode(gl_PrimitiveID);
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
        // set output data
        o_Patch[gl_InvocationID].packedData  = vec4[4](
            u_ModelViewProjectionMatrix * vec4(triangleVertices[0].xy, 0.0f, 1.0f),
            u_ModelViewProjectionMatrix * vec4(triangleVertices[1].xy, 0.0f, 1.0f),
            u_ModelViewProjectionMatrix * vec4(triangleVertices[2].xy, 0.0f, 1.0f),
            vec4(triangleVertices[0].xy, triangleVertices[1].xy)
        );

        // reverse triangle winding depending on the subdivision level
        if ((node.depth & 1) == 0) {
            vec4 tmp1 = o_Patch[gl_InvocationID].packedData[0];

            o_Patch[gl_InvocationID].packedData[0] = o_Patch[gl_InvocationID].packedData[2];
            o_Patch[gl_InvocationID].packedData[2] = tmp1;
            o_Patch[gl_InvocationID].packedData[3].xy = triangleVertices[2].xy;
        }

        // set tess levels
        gl_TessLevelInner[0] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] = TERRAIN_PATCH_TESS_FACTOR;
    } else {
        gl_TessLevelInner[0] =
        gl_TessLevelInner[1] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] = 0.0;
    }
}
#endif

/*******************************************************************************
 * Tessellation Evaluation Shader
 *
 * This tessellaction evaluation shader is responsible for placing the
 * geometry properly on the input mesh (here a terrain).
 */
#ifdef TESS_EVALUATION_SHADER
layout (triangles, ccw, equal_spacing) in;
in PatchData {
    vec4 packedData[4];
} i_Patch[];

layout(location = 0) out vec2 o_TexCoord;

void main()
{
    // unpack triangle attributes
    vec4 trianglePositions[3] = vec4[3](
        i_Patch[0].packedData[0],
        i_Patch[0].packedData[1],
        i_Patch[0].packedData[2]
    );
    vec2 tmp = i_Patch[0].packedData[3].xy - i_Patch[0].packedData[3].zw;
    vec2 triangleTexCoords[3] = vec2[3](
        i_Patch[0].packedData[3].xy,
        i_Patch[0].packedData[3].zw,
        i_Patch[0].packedData[3].zw + vec2(tmp.y, -tmp.x)
    );

    // compute final vertex attributes
    ClipSpaceAttribute attrib = TessellateClipSpaceTriangle(
        trianglePositions,
        triangleTexCoords,
        gl_TessCoord.xy
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
}
#endif
