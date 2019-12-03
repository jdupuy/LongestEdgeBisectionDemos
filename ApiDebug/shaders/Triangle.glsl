layout (std430, binding = 0)
buffer TreeBuffer {
    uint u_NodeIDs[];
};

#ifdef VERTEX_SHADER
void main()
{
    uint nodeID = u_NodeIDs[gl_InstanceID];
    leb_Node node = leb_Node(nodeID, findMSB(nodeID));
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
#if defined(MODE_TRIANGLE)
    mat2x3 posMatrix = leb_DecodeNodeAttributeArray(node, mat2x3(xPos, yPos));
#elif defined(MODE_QUAD)
    mat2x3 posMatrix = leb_DecodeNodeAttributeArray_Quad(node, mat2x3(xPos, yPos));
#endif
    vec2 pos = vec2(posMatrix[0][gl_VertexID], posMatrix[1][gl_VertexID]);

    gl_Position = vec4((2.0 * pos - 1.0), 0.0, 1.0);
}
#endif

#ifdef GEOMETRY_SHADER
layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;
layout (location = 0) noperspective out vec3 o_Distance;

void main()
{
    vec2 p0 = 800.0 * gl_in[0].gl_Position.xy;
    vec2 p1 = 800.0 * gl_in[1].gl_Position.xy;
    vec2 p2 = 800.0 * gl_in[2].gl_Position.xy;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    for (int i = 0; i < 3; ++i) {
        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
#endif

#ifdef FRAGMENT_SHADER
layout (location = 0) noperspective in vec3 i_Distance;
layout (location = 0) out vec4 o_FragColor;

void main()
{
    vec4 color = vec4(132.0 / 255.0, 198.0 / 255.0, 154.0 / 255.0, 1.0);
    vec4 wire = vec4(1.0, 1.0, 1.0, 1.0);
    float wirescale = 2.0; // scale of the wire
    vec3 d2 = i_Distance * i_Distance;
    float nearest = min(min(d2.x, d2.y), d2.z);
    float f = exp2(-nearest / wirescale);

    o_FragColor = mix(color, wire, f);
}
#endif
