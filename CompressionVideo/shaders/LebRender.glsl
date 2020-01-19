uniform sampler2DArray u_ImageSampler;
uniform sampler2DArray u_DensitySampler;
uniform vec2 u_FramebufferResolution;
uniform mat4 u_ModelViewProjectionMatrix;
uniform float u_FrameID;

/*******************************************************************************
 * DecodeTriangleVertices -- Decodes the triangle vertices in local space
 *
 */
vec2[3] DecodeTriangleVertices(in const leb_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
    vec2 p1 = vec2(pos[0][0], pos[1][0]);
    vec2 p2 = vec2(pos[0][1], pos[1][1]);
    vec2 p3 = vec2(pos[0][2], pos[1][2]);

    return vec2[3](p1, p2, p3);
}

vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

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
 * The vertex shader is empty
 */
#ifdef GEOMETRY_SHADER
layout(points) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) out vec2 o_TexCoord;
layout(location = 1) flat out uvec4 o_NodeIDs;
layout(location = 2) noperspective out vec3 o_Distance;

void main()
{
    const int lebID = 0;
#if 0
    leb_NodeAndNeighbors nn = leb_DecodeNodeAndNeighbors(lebID, gl_PrimitiveIDIn);
    leb_Node node = nn.node;
#else
    leb_Node node = leb_DecodeNode(lebID, gl_PrimitiveIDIn);
#endif
    vec2 triangleVertices[3] = DecodeTriangleVertices(node);
    vec4 triangleClipSpaceVertices[3] = vec4[3](
        u_ModelViewProjectionMatrix * vec4(triangleVertices[0], 0.0f, 1.0f),
        u_ModelViewProjectionMatrix * vec4(triangleVertices[1], 0.0f, 1.0f),
        u_ModelViewProjectionMatrix * vec4(triangleVertices[2], 0.0f, 1.0f)
    );
    vec2 p0 = u_FramebufferResolution * triangleClipSpaceVertices[0].xy / triangleClipSpaceVertices[0].w;
    vec2 p1 = u_FramebufferResolution * triangleClipSpaceVertices[1].xy / triangleClipSpaceVertices[1].w;
    vec2 p2 = u_FramebufferResolution * triangleClipSpaceVertices[2].xy / triangleClipSpaceVertices[2].w;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    leb_SameDepthNeighborIDs nodeIDs = leb_DecodeSameDepthNeighborIDs_Quad(node);
    leb_NodeAndNeighbors nn = leb__NodeAndNeighborsFromSameDepthNeighborIDs(lebID, nodeIDs, node.depth);
    o_NodeIDs = uvec4(nn.left.id, nn.right.id, nn.edge.id, nn.node.id);

    for (int i = 0; i < 3; ++i) {
        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        o_TexCoord = triangleVertices[i];
        gl_Position = triangleClipSpaceVertices[i];
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
layout(location = 1) flat in uvec4 i_NodeIDs;
layout(location = 2) noperspective in vec3 i_Distance;

layout(location = 0) out vec3 o_FragColor;

float SphericalAngle(vec3 w1, vec3 w2, vec3 w3)
{
    vec3 w12 = normalize((w2 - w1) - w1 * dot(w2 - w1, w1));
    vec3 w13 = normalize((w3 - w1) - w1 * dot(w3 - w1, w1));

    return acos(dot(w12, w13));
}

float TriangleSolidAngle(vec3 w1, vec3 w2, vec3 w3)
{
    const float pi = 3.141592653589793238f;
    float alpha1 = SphericalAngle(w1, w2, w3);
    float alpha2 = SphericalAngle(w2, w1, w3);
    float alpha3 = SphericalAngle(w3, w1, w2);

    return max(0.0f, alpha1 + alpha2 + alpha3 - pi);
}

vec3 PlaneToSphericalVertex(vec2 p)
{
    return normalize(vec3(p, 1));
}

float CauchyIntegralStd(in const vec2 triangle[3])
{
    vec3 w1 = PlaneToSphericalVertex(triangle[0]);
    vec3 w2 = PlaneToSphericalVertex(triangle[1]);
    vec3 w3 = PlaneToSphericalVertex(triangle[2]);

    return TriangleSolidAngle(w1, w2, w3);
}

float CauchyIntegral(in const vec2 triangle[3], vec2 a, vec2 b)
{
    vec2 triangleStd[3] = vec2[3]((triangle[0] - a) / b,
                                  (triangle[1] - a) / b,
                                  (triangle[2] - a) / b);

    return CauchyIntegralStd(triangleStd);
}

vec3
FilterTriangles(
    in const leb_NodeAndNeighbors nodes,
    in const vec3 nodeColors[4],
    vec2 pixelCoord,
    float filterWidth
) {
    vec2 triangle[3] = DecodeTriangleVertices(nodes.node);
    float weight = CauchyIntegral(triangle, pixelCoord, vec2(filterWidth));
    vec3 color = weight * nodeColors[3];
    float nrm = weight;

#if 1
    if (!leb_IsNullNode(nodes.left)) {
        triangle = DecodeTriangleVertices(nodes.left);
        weight = CauchyIntegral(triangle, pixelCoord, vec2(filterWidth));

        color+= weight * nodeColors[0];
        nrm+= weight;
    }

    if (!leb_IsNullNode(nodes.right)) {
        triangle = DecodeTriangleVertices(nodes.right);
        weight = CauchyIntegral(triangle, pixelCoord, vec2(filterWidth));

        color+= weight * nodeColors[1];
        nrm+= weight;
    }

    if (!leb_IsNullNode(nodes.edge)) {
        triangle = DecodeTriangleVertices(nodes.edge);
        weight = CauchyIntegral(triangle, pixelCoord, vec2(filterWidth));

        color+= weight * nodeColors[2];
        nrm+= weight;
    }
#endif

    return color / nrm;
}

leb_Node NodeCtor(uint id)
{
    return (id == 0) ? leb_Node(0u, 0) : leb_Node(id, findMSB(id));
}

vec3 NodeColor(in const leb_Node node)
{
    vec2 triangleVertices[3] = DecodeTriangleVertices(node);
    float textureLodCount = float(textureQueryLevels(u_ImageSampler));
    float lodOffset = float(node.depth - 1.0f) / 2.0f;
    vec2 texCoord = BarycentricInterpolation(triangleVertices, vec2(0.3333f));
    float texLod = textureLodCount - lodOffset;

    return textureLod(u_ImageSampler, vec3(texCoord, u_FrameID), texLod / 1.5f).rgb;
    //return textureLod(u_ImageSampler, vec3(texCoord, u_FrameID), texLod).rgb;
}

void main()
{
    const float wireScale = 1.0; // scale of the wire in pixel
    vec3 wireColor = vec3(0.7, 0.7, 0.7);
    vec3 distanceSquared = i_Distance * i_Distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);
    // get triangle data
    vec2 pixelCoord = i_TexCoord;
    leb_NodeAndNeighbors nodes = leb_NodeAndNeighbors(
        NodeCtor(i_NodeIDs.x),
        NodeCtor(i_NodeIDs.y),
        NodeCtor(i_NodeIDs.z),
        NodeCtor(i_NodeIDs.w)
    );
    vec3 nodeColors[4] = vec3[4](
        NodeColor(nodes.left),
        NodeColor(nodes.right),
        NodeColor(nodes.edge),
        NodeColor(nodes.node)
    );
    float filterRadius =  exp2(-float(nodes.node.depth) / 1.45f);
    vec3 color = FilterTriangles(nodes, nodeColors, pixelCoord, filterRadius);

#if 0
    vec2 xStd = 2.0f * i_TexCoord - 1.0f;
    float b = 1e-3;
    color = vec3(1.0f) / (1.0f + dot(xStd, xStd) / (b * b));
#endif

    color = texture(u_ImageSampler, vec3(i_TexCoord, 0)).rgb;
    //texel = vec3(E1);
#if FLAG_WIRE
    o_FragColor = mix(HdrToLdr(color), wireColor, blendFactor);
#else
    o_FragColor = HdrToLdr(color);
#endif
}
#endif
