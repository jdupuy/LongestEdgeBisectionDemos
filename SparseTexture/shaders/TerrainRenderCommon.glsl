
/*******************************************************************************
 * Uniform Data -- Global variables for terrain rendering
 *
 */
layout(std140, column_major, binding = BUFFER_BINDING_TERRAIN_VARIABLES)
uniform PerFrameVariables {
    mat4 u_ModelViewMatrix;
    mat4 u_ModelViewProjectionMatrix;
    vec4 u_FrustumPlanes[6];
    vec2 u_LodFactor;
};

uniform sampler2DArray u_LebTexture;
uniform sampler2D      u_LebRefTexture;

//uniform sampler2DArray u_LebTextureSparse;

layout(binding = BUFFER_BINDING_LEB_TEXTURE_HANDLES)
uniform LebTextureHandleBuffer {
    uvec2 u_LebTextureHandles[2048];
};


/*******************************************************************************
 * DecodeTriangleVertices -- Decodes the triangle vertices in local space
 *
 */
vec4[3] DecodeTriangleVertices(in const leb_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
    vec4 p1 = vec4(pos[0][0], pos[1][0], 0.0, 1.0);
    vec4 p2 = vec4(pos[0][1], pos[1][1], 0.0, 1.0);
    vec4 p3 = vec4(pos[0][2], pos[1][2], 0.0, 1.0);

    return vec4[3](p1, p2, p3);
}

/*******************************************************************************
 * TriangleLevelOfDetail -- Computes the LoD assocaited to a triangle
 *
 * This function is used to garantee a user-specific pixel edge length in
 * screen space. The reference edge length is that of the longest edge of the
 * input triangle.In practice, we compute the LoD as:
 *      LoD = 2 * log2(EdgePixelLength / TargetPixelLength)
 * where the factor 2 is because the number of segments doubles every 2
 * subdivision level.
 */
float TriangleLevelOfDetail(in const vec4[3] patchVertices)
{
    vec3 v0 = (u_ModelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u_ModelViewMatrix * patchVertices[2]).xyz;

    float sqrMagSum = dot(v0, v0) + dot(v2, v2);
    float twoDotAC = 2.0f * dot(v0, v2);
    float distanceToEdgeSqr = sqrMagSum + twoDotAC;
    float edgeLengthSqr     = sqrMagSum - twoDotAC;

    return u_LodFactor.x + log2(edgeLengthSqr) - u_LodFactor.y * log2(distanceToEdgeSqr);
}


/*******************************************************************************
 * FrustumCullingTest -- Checks if the triangle lies inside the view frutsum
 *
 * This function depends on FrustumCulling.glsl
 *
 */
bool FrustumCullingTest(in const vec4[3] patchVertices)
{
    vec3 bmin = min(min(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);
    vec3 bmax = max(max(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);

    return FrustumCullingTest(u_FrustumPlanes, bmin, bmax);
}

/*******************************************************************************
 * LevelOfDetail -- Computes the level of detail of associated to a triangle
 *
 * The first component is the actual LoD value. The second value is 0 if the
 * triangle is culled, and one otherwise.
 *
 */
vec2 LevelOfDetail(in const vec4[3] patchVertices)
{
    // culling test
    if (!FrustumCullingTest(patchVertices))
#if FLAG_CULL
        return vec2(0.0f, 0.0f);
#else
        return vec2(0.0f, 1.0f);
#endif

    // compute triangle LOD
    return vec2(TriangleLevelOfDetail(patchVertices), 1.0f);
}


/*******************************************************************************
 * BarycentricInterpolation -- Computes a barycentric interpolation
 *
 */
vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

vec4 BarycentricInterpolation(in vec4 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}


/*******************************************************************************
 * GenerateVertex -- Computes the final vertex position
 *
 */
struct ClipSpaceAttribute {
    vec4 position;
    vec2 texCoord;
};

ClipSpaceAttribute TessellateClipSpaceTriangle(
    in const vec4 vertexPositions[3],
    in const vec2 vertexTexCoords[3],
    in vec2 tessellationCoordinate
) {
    // compute final attributes
    vec4 position = BarycentricInterpolation(vertexPositions, tessellationCoordinate);
    vec2 texCoord = BarycentricInterpolation(vertexTexCoords, tessellationCoordinate);

    return ClipSpaceAttribute(position, texCoord);
}


/*******************************************************************************
 * ShadeFragment -- Fragement shading routine
 *
 */
#ifdef FRAGMENT_SHADER
void triangleToSquare(inout vec2 p)
{
    if (p.x < p.y) {
        p.y+= p.x;
        p.x*= 2.0f;
    } else {
        p.x+= p.y;
        p.y*= 2.0f;
    }
}

void triangleToSquareGrad(inout vec2 p, inout vec2 dpdx, inout vec2 dpdy)
{
    if (p.x < p.y) {
        p.y+= p.x;
        p.x*= 2.0f;
        dpdy+= dpdx;
        dpdx*= 2.0f;
    } else {
        p.x+= p.y;
        p.y*= 2.0f;
        dpdx+= dpdy;
        dpdy*= 2.0f;
    }
}

vec4 ShadeFragment(vec2 texCoord)
{
#if defined(SHADING_TEXTURE)
    const int lebID = 0;

#   if defined(SAMPLER_BILINEAR)
    vec2 P = texCoord;
    leb_Node node = leb_BoundingNode_Quad(lebID, texCoord, P);
    triangleToSquare(P);


    uint bitID = leb_EncodeNode(lebID, node);
    //bitID = bitID > 256u ? 0u : bitID;

#if 1
    return texture(u_LebTexture, vec3(P, u_LebTextureHandles[bitID]));
#else
    return texture(sampler2D(u_LebTextureHandles[bitID]), P);
#endif

#   elif defined(SAMPLER_TRILINEAR)
    vec2 P = texCoord;
    vec2 dPdx = dFdx(texCoord);
    vec2 dPdy = dFdy(texCoord);
    leb_Node node = leb_BoundingNode_Quad(lebID, P, dPdx, dPdy);

    //dPdx = dFdx(texCoord) * float((node.depth >> 1));
    //dPdy = dFdy(texCoord) * float((node.depth >> 1));

    // lookup current triangle
    triangleToSquareGrad(P, dPdx, dPdy);

    return textureGrad(u_LebTexture, vec3(P, node.id), dPdx, dPdy);

#if 0
    // lookup neighbors
    vec2 Q_L = vec2(-P.x, P.y);
    triangleToSquare(Q_L);
    textureData+= 0.0f * texture(u_LebTexture, vec3(Q_L, nodeAndNeighbors.left.id));

    vec2 Q_R = vec2(P.x, -P.y);
    triangleToSquare(Q_R);
    textureData+= 0.0f * texture(u_LebTexture, vec3(Q_R, nodeAndNeighbors.right.id));

    vec2 Q_E = 1.0f - P;
    triangleToSquare(Q_E);
    textureData+= 0.0f * texture(u_LebTexture, vec3(Q_E, nodeAndNeighbors.edge.id));
#endif

#   else
    return vec4(1, 0, 0, 1);
#   endif


#elif defined(SHADING_TEXTURE_REF)
    return texture(u_LebRefTexture, texCoord);


#elif defined(SHADING_COLOR)
    return vec4(1, 1, 1, 1);


#else
    return vec4(1, 0, 0, 1);
#endif
}
#endif