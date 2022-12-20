//////////////////////////////////////////////////////////////////////////////
//
// Longest Edge Bisection (LEB) Subdivision Demo
//
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"
#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <stdexcept>
#include <vector>
#include <array>

#define LEB_IMPLEMENTATION
#include "LongestEdgeBisection.h"

#define VIEWPORT_WIDTH 800

#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);

char *strcat2(char *dst, const char *src1, const char *src2)
{
    strcpy(dst, src1);

    return strcat(dst, src2);
}

// -----------------------------------------------------------------------------
struct AppManager {
    struct {
        const char *shader;
        const char *output;
    } dir;
} g_app = {
    /*dir*/    {PATH_TO_SRC_DIRECTORY "./shaders/", "./"},
};

enum { PROGRAM_POINT, PROGRAM_TRIANGLE, PROGRAM_COUNT};
struct OpenGLManager {
    GLuint vertexArray;
    GLuint nodeBuffer;
    GLuint texture;
    GLuint programs[PROGRAM_COUNT];
} g_gl = {0, 0, 0, {0}};

enum {MODE_TRIANGLE, MODE_QUAD};
struct DemoParameters {
    int mode;
    int minDepth, maxDepth;
    uint32_t activeNode;
    dja::vec2 target;
    float radius;
    struct {bool reset, freeze;} flags;
} g_params = {
    MODE_TRIANGLE, 1, 5, 0, dja::vec2(0.4f, 0.1f), 0.0f, {true, false}
};

// -----------------------------------------------------------------------------
float wedge(const dja::vec2& a, const dja::vec2& b)
{
    return a.x * b.y - a.y * b.x;
}

struct triangle {
    dja::vec2 v[3];
    triangle(const dja::vec2& a, const dja::vec2& b, const dja::vec2& c) {
        v[0].x = a.x; v[0].y = a.y;
        v[1].x = b.x; v[1].y = b.y;
        v[2].x = c.x; v[2].y = c.y;
    }

    bool contains(const dja::vec2 &p) const {
        const float e = 0.0f;
        float w1 = wedge(v[1] - v[0], p - v[0]);
        float w2 = wedge(v[2] - v[1], p - v[1]);
        float w3 = wedge(v[0] - v[2], p - v[2]);

        return (w1 <= e && w2 <= e && w3 <= e) || (w1 >= -e && w2 >= -e && w3 >= -e);
    }

    bool contains(const dja::vec2 &p, float r) const {
        bool res = contains(p);
        int cnt = 256;

        for (int i = 0; i < cnt && !res; ++i) {
            float u = i / float(cnt);
            float a = u * 2 * M_PI;
            float x = cos(a), y = sin(a);

            res = res || contains(p + r * dja::vec2(x, y));
        }

        return res;
    }
};

// triangle
struct bintree {
    leb_Heap *m_leb;
    int m_pingPong;

    bintree() {
        m_leb = leb_CreateMinMax(g_params.minDepth, g_params.maxDepth);
        m_pingPong = 0;
        leb_ResetToRoot(m_leb);
    }

    void reset(int minDepth, int maxDepth) {
        leb_Release(m_leb);
        m_leb = leb_CreateMinMax(minDepth, maxDepth);
        leb_ResetToRoot(m_leb);
    }

    void build(const dja::vec2 &target, int maxLevel) {
        leb_ResetToRoot(m_leb);

        for (int i = 0; i < maxLevel; ++i) {
            updateOnce(target);
        }
    }

    bool testTarget(const leb_Node &node, const dja::vec2 &target) const
    {
        float attribArray[][3] = {
            {0.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f}
        };

        if (g_params.mode == MODE_TRIANGLE)
            leb_DecodeNodeAttributeArray(node, 2, attribArray);
        else
            leb_DecodeNodeAttributeArray_Quad(node, 2, attribArray);

        dja::vec2 a = dja::vec2(attribArray[0][0], attribArray[1][0]),
                  b = dja::vec2(attribArray[0][1], attribArray[1][1]),
                  c = dja::vec2(attribArray[0][2], attribArray[1][2]);

        return triangle(a, b, c).contains(target, g_params.radius);
    }

    void updateOnce(const dja::vec2 &target)
    {
        uint32_t cnt = leb_NodeCount(m_leb);

        // update
        for (uint32_t i = 0; i < cnt; ++i) {
            leb_Node node = leb_DecodeNode(m_leb, i);

            if /* splitting pass */(m_pingPong == 0 && !g_params.flags.freeze) {
                bool isInside = testTarget(node, target);

                /* split */
                if (isInside) {
                    if (g_params.mode == MODE_TRIANGLE) {
                        leb_SplitNodeConforming(m_leb, node);
                    } else {
                        leb_SplitNodeConforming_Quad(m_leb, node);
                    }
                }
            } else if /* merging pass */(m_pingPong == 1 && !g_params.flags.freeze) {
                leb_DiamondParent diamond;

                if (g_params.mode == MODE_TRIANGLE) {
                    diamond = leb_DecodeDiamondParent(node);
                } else {
                    diamond = leb_DecodeDiamondParent_Quad(node);
                }

                bool shouldMerge = !testTarget(diamond.base, target)
                                 && !testTarget(diamond.top, target);

                if (shouldMerge) {
                    if (g_params.mode == MODE_TRIANGLE) {
                        leb_MergeNodeConforming(m_leb, node, diamond);
                    } else {
                        leb_MergeNodeConforming_Quad(m_leb, node, diamond);
                    }
                }
            }
        }

        leb_ComputeSumReduction(m_leb);

        precomputeNodes();
        m_pingPong = 1 - m_pingPong;
    }


    std::vector<uint32_t> precomputeNodes() const
    {
        uint32_t nodeCount = leb_NodeCount(m_leb);
        std::vector<uint32_t> dataOut(nodeCount);

        for (uint32_t i = 0; i < nodeCount; ++i) {
            uint32_t nodeID = leb_DecodeNode(m_leb, i).id;

            dataOut[i] = nodeID;
        }

        return dataOut;
    }

    int size() const {
        return (int)leb_NodeCount(m_leb);
    }

} g_bintree;


// -----------------------------------------------------------------------------

void loadNodeBuffer()
{
    const std::vector<uint32_t> nodeIDs = g_bintree.precomputeNodes();

    if (glIsBuffer(g_gl.nodeBuffer))
        glDeleteBuffers(1, &g_gl.nodeBuffer);
    glGenBuffers(1, &g_gl.nodeBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_gl.nodeBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 sizeof(nodeIDs[0]) * nodeIDs.size(),
                 &nodeIDs[0],
                 GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_gl.nodeBuffer);
}

void loadEmptyVertexArray()
{
    if (glIsVertexArray(g_gl.vertexArray))
        glDeleteVertexArrays(1, &g_gl.vertexArray);
    glGenVertexArrays(1, &g_gl.vertexArray);
    glBindVertexArray(g_gl.vertexArray);
    glBindVertexArray(0);
}

// -----------------------------------------------------------------------------
void loadPointProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_POINT];
    char buf[1024];

    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "Target.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        throw std::runtime_error("shader creation error");
    }
    djgp_release(djp);
}

void loadTriangleProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_TRIANGLE];
    char buf[1024];

    if (g_params.mode == MODE_TRIANGLE)
        djgp_push_string(djp, "#define MODE_TRIANGLE\n");
    else
        djgp_push_string(djp, "#define MODE_QUAD\n");

    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "Triangle.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        throw std::runtime_error("shader creation error");
    }
    djgp_release(djp);
}


// -----------------------------------------------------------------------------
// allocate resources
// (typically before entering the game loop)
void load(int /*argc*/, char **/*argv*/)
{
    g_bintree.build(g_params.target, g_params.maxDepth);

    loadEmptyVertexArray();
    loadNodeBuffer();
    loadPointProgram();
    loadTriangleProgram();
}

// free resources
// (typically after exiting the game loop but before context deletion)
void release()
{
    glDeleteVertexArrays(1, &g_gl.vertexArray);
    for (int i = 0; i < PROGRAM_COUNT; ++i)
        glDeleteProgram(g_gl.programs[i]);
    glDeleteBuffers(1, &g_gl.nodeBuffer);
}

// -----------------------------------------------------------------------------
void renderTriangleScene()
{
    // triangles
    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    glLineWidth(1.f);
    glUseProgram(g_gl.programs[PROGRAM_TRIANGLE]);
    glBindVertexArray(g_gl.vertexArray);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, g_bintree.size());
    glBindVertexArray(0);

    // target helper
    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    glDisable(GL_CULL_FACE);
    glUseProgram(g_gl.programs[PROGRAM_POINT]);
    glUniform2f(
        glGetUniformLocation(g_gl.programs[PROGRAM_POINT], "u_Target"),
        g_params.target.x, g_params.target.y
    );
    glPointSize(11.f);
    glBindVertexArray(g_gl.vertexArray);
        glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);

    glUseProgram(0);
}

// -----------------------------------------------------------------------------
void render()
{
    g_bintree.updateOnce(g_params.target);
    loadNodeBuffer();

    glClearColor(0.8, 0.8, 0.8, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderTriangleScene();
}

// -----------------------------------------------------------------------------
void renderGui()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0)/*, ImGuiSetCond_FirstUseEver*/);
    ImGui::SetNextWindowSize(ImVec2(256, VIEWPORT_WIDTH)/*, ImGuiSetCond_FirstUseEver*/);
    ImGui::Begin("Window");
    {
        const char* eModes[] = {
            "Triangle",
            "Quad"
        };
        if (ImGui::Combo("Mode", &g_params.mode, &eModes[0], 2)) {
            loadTriangleProgram();
            g_bintree.reset(g_params.minDepth, g_params.maxDepth);
        }
        if (ImGui::SliderInt("MinDepth", &g_params.minDepth, 0, g_params.maxDepth)) {
            g_bintree.reset(g_params.minDepth, g_params.maxDepth);
        }
        if (ImGui::SliderInt("MaxDepth", &g_params.maxDepth, std::max(5, g_params.minDepth), 29)) {
            g_bintree.reset(g_params.minDepth, g_params.maxDepth);
        }
        ImGui::SliderFloat("TargetX", &g_params.target.x, 0, 1);
        ImGui::SliderFloat("TargetY", &g_params.target.y, 0, 1);
        ImGui::SliderFloat("Radius", &g_params.radius, 0, 1);
        if (ImGui::Button("Reset Tree")) {
            g_bintree.build(g_params.target, g_params.maxDepth);
            loadNodeBuffer();
        }
        ImGui::Checkbox("Freeze", &g_params.flags.freeze);
        ImGui::Text("Mem Usage: %u Bytes", leb__HeapByteSize(g_params.maxDepth));
        ImGui::Text("Nodes: %u", g_bintree.size());
        ImGui::Text("Bounding Node: %u",
                    g_params.mode == MODE_TRIANGLE ?
                                    leb_BoundingNode(g_bintree.m_leb,
                                                     g_params.target.x,
                                                     g_params.target.y).id
                                  : leb_BoundingNode_Quad(g_bintree.m_leb,
                                                          g_params.target.x,
                                                          g_params.target.y).id);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// -----------------------------------------------------------------------------
void
keyboardCallback(
    GLFWwindow* window,
    int key, int /*scancode*/, int action, int /*mods*/
) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, GL_TRUE);
            break;
            default: break;
        }
    }
}

void
mouseButtonCallback(GLFWwindow* /*window*/, int /*button*/, int /*action*/, int /*mods*/)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}

void mouseMotionCallback(GLFWwindow* /*window*/, double x, double y)
{
    static double x0 = 0, y0 = 0;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    x0 = x;
    y0 = y;
}

void mouseScrollCallback(GLFWwindow* /*window*/, double, double)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}


// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
    leb_Node node = {27, 4};
    leb_SameDepthNeighborIDs ids = leb_DecodeSameDepthNeighborIDs(node);
    printf("%i %i %i %i\n", ids.left, ids.right, ids.edge, ids._reserved);
    abort();

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
    // Create the Window
    LOG("Loading {Window-Main}\n");
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH+256, VIEWPORT_WIDTH, "Hello Imgui", NULL, NULL);
    if (window == NULL) {
        LOG("=> Failure <=\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, &keyboardCallback);
    glfwSetCursorPosCallback(window, &mouseMotionCallback);
    glfwSetMouseButtonCallback(window, &mouseButtonCallback);
    glfwSetScrollCallback(window, &mouseScrollCallback);

    // Load OpenGL functions
    LOG("Loading {OpenGL}\n");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("gladLoadGLLoader failed\n");
        return -1;
    }

    LOG("-- Begin -- Demo\n");
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 450");
        load(argc, argv);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            render();
            renderGui();

            glfwSwapBuffers(window);
        }

        release();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
    } catch (std::exception& e) {
        LOG("%s", e.what());
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    } catch (...) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    LOG("-- End -- Demo\n");

    return 0;
}


