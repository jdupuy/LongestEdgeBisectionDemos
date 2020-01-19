
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

vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u)
{
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

#ifdef VERTEX_SHADER
layout(location = 0) in float i_Random;

leb_Node randomToLebNode(int lebID, float u)
{
    const int fixedDepth = 8;
    uint minFixedDepthNodeID = 1u << fixedDepth;
    float nodeID = u * float(leb_NodeCount(lebID));
    float a = floor(nodeID);
    float b = nodeID - a;
    uint leafID = uint(a);
    leb_Node node = leb_DecodeNode(lebID, leafID);
    uint finalBits = uint(b * float(minFixedDepthNodeID));

    node.depth+= fixedDepth;
    node.id = (node.id << fixedDepth) | finalBits;

    return node;
}

void main()
{
    const int lebID = 0;
#if 0
    float u = i_Random; // in [0, 1)
    float tmp = u * float(leb_NodeCount(lebID));
    uint nodeID = uint(tmp);
    leb_Node node = leb_DecodeNode(lebID, nodeID);
#else
    leb_Node node = randomToLebNode(lebID, i_Random);
#endif
    vec2[3] triangleVertices = DecodeTriangleVertices(node);
    vec2 samplePos = BarycentricInterpolation(triangleVertices, vec2(0.33f));

    gl_Position = u_ModelViewProjectionMatrix * vec4(samplePos, 0.0f, 1.0f);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) out vec3 o_FragColor;

void main()
{
    vec2 tmp = 2.0 * gl_PointCoord.xy - 1.0;
    float r = dot(tmp, tmp);
    if (r > 1.0)
        discard;

    o_FragColor = mix(vec3(0.9), vec3(0), smoothstep(0.25f, 0.5f, r));
}

#endif

