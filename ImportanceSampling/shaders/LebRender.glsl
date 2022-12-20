uniform sampler2D u_ImageSampler;
uniform sampler2D u_DensitySampler;
uniform vec2 u_FramebufferResolution;
uniform mat4 u_ModelViewProjectionMatrix;

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
layout(location = 1) noperspective out vec3 o_Distance;

void main()
{
    const int lebID = 0;
    leb_Node node = leb_DecodeNode(lebID, gl_PrimitiveIDIn);
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
layout(location = 1) noperspective in vec3 i_Distance;
layout(location = 0) out vec3 o_FragColor;

void main()
{
    const float wireScale = 1.0; // scale of the wire in pixel
    vec3 wireColor = vec3(0.0, 0.0, 0.0);
    vec3 distanceSquared = i_Distance * i_Distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);
    float E1 = texture(u_DensitySampler, i_TexCoord).r;
    float E2 = texture(u_DensitySampler, i_TexCoord).g;
    float Var = clamp(E2 - E1 * E1, 0.0f, 1.0f);
    vec3 texel = 1.0*texture(u_ImageSampler, i_TexCoord).rgb;

#if FLAG_WIRE
    o_FragColor = mix(HdrToLdr(texel), wireColor, blendFactor);
#else
    o_FragColor = HdrToLdr(texel);
#endif
}
#endif
