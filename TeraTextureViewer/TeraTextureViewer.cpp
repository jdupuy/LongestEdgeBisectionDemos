/////////////////////////////////////////////////////////////////////////////
//
// Longest Edge Bisection (LEB) Subdivision Demo
//
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <stdexcept>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TT_IMPLEMENTATION
#include "TeraTexture.h"

#define LEB_IMPLEMENTATION
#include "LongestEdgeBisection.h"

#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"

#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#define VIEWPORT_WIDTH 800

#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);

#ifndef M_PI
#define M_PI 3.141592654
#endif
#define BUFFER_SIZE(x)    ((int)(sizeof(x)/sizeof(x[0])))
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

char *strcat2(char *dst, const char *src1, const char *src2)
{
    strcpy(dst, src1);

    return strcat(dst, src2);
}

static void APIENTRY
DebugOutputLogger(
    GLenum source,
    GLenum type,
    GLuint,
    GLenum severity,
    GLsizei,
    const GLchar* message,
    const GLvoid*
) {
    char srcstr[32], typestr[32];

    switch(source) {
        case GL_DEBUG_SOURCE_API: strcpy(srcstr, "OpenGL"); break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: strcpy(srcstr, "Windows"); break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: strcpy(srcstr, "Shader Compiler"); break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: strcpy(srcstr, "Third Party"); break;
        case GL_DEBUG_SOURCE_APPLICATION: strcpy(srcstr, "Application"); break;
        case GL_DEBUG_SOURCE_OTHER: strcpy(srcstr, "Other"); break;
        default: strcpy(srcstr, "???"); break;
    };

    switch(type) {
        case GL_DEBUG_TYPE_ERROR: strcpy(typestr, "Error"); break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: strcpy(typestr, "Deprecated Behavior"); break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: strcpy(typestr, "Undefined Behavior"); break;
        case GL_DEBUG_TYPE_PORTABILITY: strcpy(typestr, "Portability"); break;
        case GL_DEBUG_TYPE_PERFORMANCE: strcpy(typestr, "Performance"); break;
        case GL_DEBUG_TYPE_OTHER: strcpy(typestr, "Message"); break;
        default: strcpy(typestr, "???"); break;
    }

    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        LOG("djg_error: %s %s\n"                \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    } /*else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    }*/
}

void SetupDebugOutput(void)
{
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&DebugOutputLogger, NULL);
}

// -----------------------------------------------------------------------------
struct AppManager {
    struct {
        const char *shader;
        const char *output;
    } dir;
    struct {
        int on, frame, capture;
    } recorder;
    int frame, frameLimit;
} g_app = {
    /*dir*/ {
        PATH_TO_SRC_DIRECTORY "./shaders/",
        "./"
    },
    /*record*/ {
        false, 0, 0
    },
    /*frame*/  0, -1
};

struct OpenGLManager {
    GLuint vertexArray;
    GLuint program;
} g_gl = {0, 0};

enum {
    TONEMAP_UNCHARTED2,
    TONEMAP_FILMIC,
    TONEMAP_ACES,
    TONEMAP_REINHARD,
    TONEMAP_RAW
};

struct ViewerManager {
    struct {
        tt_Texture *tt;
        tt_UpdateArgs args;
        int id;
    } texture;
    struct {
        struct {float x, y;} pos;
        float zoom;
        int tonemap;
    } camera;
    struct {bool freezeTexture;} flags;
    const struct {
        int indirection, leb;
    } bufferIndex;
} g_viewer = {
    {NULL, {0}, 0},
    {
        {0.0f, 0.0f},
        0.0f,
        TONEMAP_RAW
    }, {false},
    {7, 8}
};
// -----------------------------------------------------------------------------

void LoadVertexArray()
{
    glGenVertexArrays(1, &g_gl.vertexArray);
    glBindVertexArray(g_gl.vertexArray);
    glBindVertexArray(0);
}

void LoadProgram()
{
    djg_program *djp = djgp_create();
    char buffer[256];

    djgp_push_string(djp,
                     "#define BUFFER_BINDING_LEB %i\n",
                     g_viewer.bufferIndex.leb);
    djgp_push_file(djp, strcat2(buffer, g_app.dir.shader, "LongestEdgeBisection.glsl"));
    djgp_push_string(djp,
                     "#define TT_TEXTURES_PER_PAGE %i\n",
                     tt_TexturesPerPage(g_viewer.texture.tt));
    djgp_push_string(djp,
                     "#define TT_BUFFER_BINDING_INDIRECTION %i\n",
                     g_viewer.bufferIndex.indirection);
    switch (g_viewer.camera.tonemap) {
    case TONEMAP_UNCHARTED2:
        djgp_push_string(djp, "#define TONEMAP_UNCHARTED2\n");
        break;
    case TONEMAP_FILMIC:
        djgp_push_string(djp, "#define TONEMAP_FILMIC\n");
        break;
    case TONEMAP_ACES:
        djgp_push_string(djp, "#define TONEMAP_ACES\n");
        break;
    case TONEMAP_REINHARD:
        djgp_push_string(djp, "#define TONEMAP_REINHARD\n");
        break;
    default:
        break;
    }
    djgp_push_file(djp, strcat2(buffer, g_app.dir.shader, "ToneMapping.glsl"));
    djgp_push_file(djp, strcat2(buffer, g_app.dir.shader, "TeraTexture.glsl"));
    djgp_push_file(djp, strcat2(buffer, g_app.dir.shader, "Render.glsl"));


    if (!djgp_to_gl(djp, 450, false, true, &g_gl.program)) {
        djgp_release(djp);

        throw std::runtime_error("shader compilation failed");
    }

    // upload texture samplers
    GLint locations[] = {0, 1, 2, 3};
    glProgramUniform1iv(
        g_gl.program,
        glGetUniformLocation(g_gl.program, "tt_Textures[0]"),
        tt_TexturesPerPage(g_viewer.texture.tt),
        locations
    );

    //TT_LOG("loc: %i\n", glGetUniformLocation(g_gl.program, "tt_Textures[0]"));

    djgp_release(djp);
}

void ReleaseProgram()
{
    glDeleteProgram(g_gl.program);
}

void ReleaseVertexArray()
{
    glDeleteVertexArrays(1, &g_gl.vertexArray);
}

void Load(int argc, char **argv)
{
    GLenum textureUnits[] = {
        GL_TEXTURE0,
        GL_TEXTURE1,
        GL_TEXTURE2,
        GL_TEXTURE3
    };

    g_viewer.texture.tt = tt_Load("texture.tt", /* cache size */2048);
    g_viewer.texture.args.pixelsPerTexelTarget = 1.0f;

    tt_BindPageTextures(g_viewer.texture.tt, textureUnits);

    LoadVertexArray();
    LoadProgram();
}

void Release()
{
    ReleaseProgram();
    ReleaseVertexArray();
    tt_Release(g_viewer.texture.tt);
}

void UpdateTexture()
{
    tt_UpdateArgs *args = &g_viewer.texture.args;
    float zoomFactor = exp2f(-g_viewer.camera.zoom);
    float x = g_viewer.camera.pos.x;
    float y = g_viewer.camera.pos.y;
    dja::mat4 modelView = dja::mat4::homogeneous::orthographic(
        x - zoomFactor + 0.50001f, x + zoomFactor + 0.5f,
        y - zoomFactor + 0.5f, y + zoomFactor + 0.5f,
        -1.0f, 1.0f
    );
    dja::mat4 projection = dja::mat4(1.0f);
    dja::mat4 mvp = projection * modelView;

    modelView = dja::transpose(modelView);
    mvp = dja::transpose(mvp);

    //
    memcpy(args->matrices.modelView, &modelView, sizeof(modelView));
    memcpy(args->matrices.modelViewProjection, &mvp, sizeof(mvp));
    args->projection = TT_PROJECTION_ORTHOGRAPHIC;
    args->worldSpaceImagePlaneAtUnitDepth.width = 2.0f * zoomFactor;
    args->worldSpaceImagePlaneAtUnitDepth.height = 2.0f * zoomFactor;
    args->framebuffer.width = VIEWPORT_WIDTH;
    args->framebuffer.height= VIEWPORT_WIDTH;

    // updload MVP
    glProgramUniformMatrix4fv(
        g_gl.program,
        glGetUniformLocation(g_gl.program, "u_ModelViewProjection"),
        1, GL_FALSE, &mvp[0][0]
    );

    if (!g_viewer.flags.freezeTexture)
        tt_Update(g_viewer.texture.tt, args);
}

void Render()
{
    UpdateTexture();

    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     g_viewer.bufferIndex.indirection,
                     tt_IndirectionBuffer(g_viewer.texture.tt));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     g_viewer.bufferIndex.leb,
                     tt_LebBuffer(g_viewer.texture.tt));
    glUseProgram(g_gl.program);
    glUniform1i(glGetUniformLocation(g_gl.program, "u_PageTextureID"),
                g_viewer.texture.id);
    glBindVertexArray(g_gl.vertexArray);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, g_viewer.bufferIndex.indirection, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, g_viewer.bufferIndex.leb, 0);
}

void RenderGui()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(256, VIEWPORT_WIDTH));
    ImGui::Begin("Window");
    {
        int texturesPerPage = tt_TexturesPerPage(g_viewer.texture.tt);
        const char* eTonemaps[] = {
            "Uncharted2",
            "Filmic",
            "Aces",
            "Reinhard",
            "Raw"
        };
        if (ImGui::Combo("Tonemap", &g_viewer.camera.tonemap, &eTonemaps[0], BUFFER_SIZE(eTonemaps)))
            LoadProgram();
        ImGui::Text("Pos : %f %f", g_viewer.camera.pos.x, g_viewer.camera.pos.y);
        ImGui::Text("Zoom: %f", g_viewer.camera.zoom);
        if (texturesPerPage > 1) {
            const char* eTextureIDs[] = {
                "TEX0", "TEX1", "TEX2", "TEX3", "TEX4", "TEX5", "TEX6", "TEX7"
            };
            ImGui::Combo("PageTextureID", &g_viewer.texture.id, &eTextureIDs[0], tt_TexturesPerPage(g_viewer.texture.tt));
        }
        ImGui::SliderFloat("PixelPerTexel", &g_viewer.texture.args.pixelsPerTexelTarget, 0, 4);
        ImGui::Checkbox("Freeze", &g_viewer.flags.freezeTexture);
        ImGui::Text("NodeCount: %i", leb_NodeCount(g_viewer.texture.tt->cache.leb));
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // screen recording
    if (g_app.recorder.on) {
        char name[64], path[1024];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        sprintf(name, "capture_%02i_%09i",
                g_app.recorder.capture,
                g_app.recorder.frame);
        strcat2(path, g_app.dir.output, name);
        djgt_save_glcolorbuffer_bmp(GL_BACK, GL_RGB, path);
        ++g_app.recorder.frame;
    }
}

// -----------------------------------------------------------------------------
void
KeyboardCallback(
    GLFWwindow* window,
    int key,
    int /*scancode*/,
    int action,
    int /*mods*/
) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;

    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GL_TRUE);
        break;
        case GLFW_KEY_R:
            LoadProgram();
        break;
        case GLFW_KEY_C:
            if (g_app.recorder.on) {
                g_app.recorder.frame = 0;
                ++g_app.recorder.capture;
            }
            g_app.recorder.on = !g_app.recorder.on;
        break;
        default: break;
        }
    }
}

void
MouseButtonCallback(
    GLFWwindow* /*window*/,
    int /*button*/,
    int /*action*/,
    int /*mods*/
) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}

void MouseMotionCallback(GLFWwindow* window, double x, double y)
{
    static double x0 = 0, y0 = 0;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        float sc = exp2f(-g_viewer.camera.zoom);
        float dx = x - x0, dy = y - y0;

        g_viewer.camera.pos.x-= dx * sc * 2e-3;
        g_viewer.camera.pos.y+= dy * sc * 2e-3;
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        g_viewer.camera.zoom+= (x - x0) * 1e-2;

        if (g_viewer.camera.zoom < -1.0f)
            g_viewer.camera.zoom = -1.0f;
    }

    x0 = x;
    y0 = y;
}

void MouseScrollCallback(GLFWwindow* /*window*/, double, double)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}


// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
    // Create the Window
    LOG("Loading {Window-Main}\n");
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH+256, VIEWPORT_WIDTH, "Viewer", NULL, NULL);
    if (window == NULL) {
        LOG("=> Failure <=\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, &KeyboardCallback);
    glfwSetCursorPosCallback(window, &MouseMotionCallback);
    glfwSetMouseButtonCallback(window, &MouseButtonCallback);
    glfwSetScrollCallback(window, &MouseScrollCallback);

    // Load OpenGL functions
    LOG("Loading {OpenGL}\n");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("gladLoadGLLoader failed\n");
        return -1;
    }

    SetupDebugOutput();

    LOG("-- Begin -- Demo\n");
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 450");
        Load(argc, argv);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            Render();
            RenderGui();

            glfwSwapBuffers(window);
        }

        Release();
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

