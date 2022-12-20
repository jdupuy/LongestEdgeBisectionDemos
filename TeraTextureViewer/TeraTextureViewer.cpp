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

#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#define TT_IMPLEMENTATION
#include "TeraTexture.h"

#define LEB_IMPLEMENTATION
#include "LongestEdgeBisection.h"

#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"

#define FLAG_CAPTURE 0
#define VIEWPORT_WIDTH  1280
#define VIEWPORT_HEIGHT 1280

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
#if FLAG_CAPTURE
        true, 0, 0
#else
        false, 0, 0
#endif
    },
    /*frame*/  0, -1
};

struct OpenGLManager {
    GLuint vertexArray;
    GLuint program;
    djg_clock *clock;
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
        1.0f,
        TONEMAP_FILMIC
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
#if 0
    g_viewer.texture.tt = tt_Load("texture.tt", /* cache size */2048);
#else
    g_viewer.texture.tt = tt_Load("/media/jdups/a7182ac4-4b59-4450-87ec-1b89a0cf1d8f/terrain.tt", /* cache size */2048);
#endif
    g_viewer.texture.args.pixelsPerTexelTarget = 1.0f;

    tt_BindPageTextures(g_viewer.texture.tt, textureUnits);

    LoadVertexArray();
    LoadProgram();
    g_gl.clock = djgc_create();
}

void Release()
{
    ReleaseProgram();
    ReleaseVertexArray();
    tt_Release(g_viewer.texture.tt);
    djgc_release(g_gl.clock);
}

float lerp(float a, float b, float u)
{
    return a + u * (b - a);
}

float toStd(float x, float xmin, float xmax)
{
    return (x - xmin) / (xmax - xmin);
}

float sinCurve(float u)
{
    return -cosf(u * M_PI) * 0.5f + 0.5f;
}

float getZoom(float u)
{
    float seqCnt = 5.0f;
    float seqFq = 1.0f / seqCnt;

    if (u < seqFq) {
        float zStart = 1.15f;
        float zEnd = 4.5f;
        u = sinCurve(toStd(u, 0.0f, seqFq));

        return lerp(zStart, zEnd, u);
    } else if (u < 2.0f * seqFq) {
        float zStart = 4.5f;
        float zEnd = 8.0f;
        u = sinCurve(toStd(u, seqFq, 2.0f * seqFq));

        return lerp(zStart, zEnd, u);
    } else if (u < 3.0f * seqFq) {
        float zStart = 8.0f;
        float zEnd = 15.0f;
        u = sinCurve(toStd(u, 2.0f * seqFq, 3.0f * seqFq));

        return lerp(zStart, zEnd, u);
    } else /*if (u < 4.0f * seqFq)*/ {
        float zStart = 15.0f;
        float zEnd = 1.15f;
        u = sinCurve(toStd(u, 3.0f * seqFq, 1.0f));

        return lerp(zStart, zEnd, u);
    }
}

void UpdateTexture()
{
    tt_UpdateArgs *args = &g_viewer.texture.args;
#if FLAG_CAPTURE
#if 0
    const float startZ = 1.15f;
    const float endZ = 15.0f;
    const int fps = 60;
    const int duration = 16;
    const int frameCount = duration * fps;
    static int frameID = 0;
    float u = (float)frameID / (frameCount - 1);

    g_viewer.camera.pos.x = 0.049372f;
    g_viewer.camera.pos.y = 0.012751f;
    g_viewer.camera.zoom  = getZoom(u);//lerp(startZ, endZ, u);
#else
    const float startZ = 1.15f;
    const float endZ = 15.0f;
    const int fps = 1;
    const int duration = 10;
    const int frameCount = duration * fps;
    static int frameID = 0;
    float u = (float)frameID / (frameCount - 1);

    g_viewer.camera.pos.x = 0.048351f;
    g_viewer.camera.pos.y = 0.012752f;
    g_viewer.camera.zoom  = lerp(startZ, endZ, u);
#endif

    //modelView = mvStart + (0.0f * mvEnd);//(mvEnd - mvStart));

    if (frameID == frameCount)
        exit(0);
#endif
    float zoomFactor = exp2f(-g_viewer.camera.zoom);
    float x = g_viewer.camera.pos.x;
    float y = g_viewer.camera.pos.y;

    dja::mat4 model = dja::mat4::homogeneous::translation(dja::vec3(-0.5f, -0.5f, 0.0f));
    dja::mat4 view = dja::mat4::homogeneous::translation(dja::vec3(x, y, 0.0f));

    dja::mat4 projection = dja::mat4::homogeneous::orthographic(
        -zoomFactor, +zoomFactor,
        -zoomFactor, +zoomFactor,
        -1.0f, 1.0f
    );
    //dja::mat4 frustumZoom = dja::mat4::homogeneous::orthographic(
    //    -1.0f, 1.0f,
    //    -1.0f, 1.0f,
    //    -1.0f, 1.0f
    //);
    dja::mat4 modelView = view * model;
    dja::mat4 mvp = projection * modelView;

    modelView = dja::transpose(modelView);
    mvp = dja::transpose(mvp);
    modelView = mvp;
    //mvp = dja::mat4(1.0f);

    //
    memcpy(args->matrices.modelView, &modelView, sizeof(modelView));
    memcpy(args->matrices.modelViewProjection, &mvp, sizeof(mvp));
    args->projection = TT_PROJECTION_ORTHOGRAPHIC;
    args->worldSpaceImagePlaneAtUnitDepth.width = 2.0f * zoomFactor;
    args->worldSpaceImagePlaneAtUnitDepth.height = 2.0f * zoomFactor * VIEWPORT_HEIGHT / (float)VIEWPORT_WIDTH;
    args->framebuffer.width = VIEWPORT_WIDTH;
    args->framebuffer.height= VIEWPORT_HEIGHT;

    // updload MVP
    glProgramUniformMatrix4fv(
        g_gl.program,
        glGetUniformLocation(g_gl.program, "u_ModelViewProjection"),
        1, GL_FALSE, &mvp[0][0]
    );

#if FLAG_CAPTURE
    if (true || frameID == 0) {
        for (int i = 0; i < 16; ++i) {
            tt_Update(g_viewer.texture.tt, args);
        }
    }
    ++frameID;
#endif
    if (!g_viewer.flags.freezeTexture)
        tt_Update(g_viewer.texture.tt, args);

}

void Render()
{
    djgc_start(g_gl.clock);
    UpdateTexture();
    djgc_stop(g_gl.clock);

    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
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
    //ImGui::ShowDemoWindow();
#if FLAG_CAPTURE == 0
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(256, 200));
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
#endif

#if 0
    ImGui::Begin("Streaming Performance");
    {
        double cpuDt, gpuDt;

        djgc_ticks(g_gl.clock, &cpuDt, &gpuDt);
        //ImGui::Text("Timing -- CPU: %.3f%s",
        //    cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
        //    cpuDt < 1. ? "ms" : " s");
        //ImGui::SameLine();
        //ImGui::Text("GPU: %.3f%s",
        //    gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
        //    gpuDt < 1. ? "ms" : " s");

        static float values_cpu[60] = { 0 };
        static float values_gpu[60] = { 0 };
        static int offset = 0;
        static float refresh_time = 0;

        if (refresh_time == 0)
          refresh_time = ImGui::GetTime();

        while (refresh_time < ImGui::GetTime())
        {
            values_cpu[offset] = cpuDt * 1e3;
            values_gpu[offset] = gpuDt * 1e3;

            offset = (offset+1) % IM_ARRAYSIZE(values_cpu);
            refresh_time+= 0.25f;
        }

        ImGui::PlotLines("CPU (ms)", values_cpu,
                         IM_ARRAYSIZE(values_cpu), offset,
                         std::to_string(cpuDt * 1e3).c_str(),
                         0.0f, 12.0f, ImVec2(320, 80));
        ImGui::PlotLines("GPU (ms)", values_gpu,
                         IM_ARRAYSIZE(values_gpu), offset,
                         std::to_string(gpuDt * 1e3).c_str(),
                         0.0f, 12.0f, ImVec2(320, 80));

        //ImGui::PlotLines("History", )
    }
    ImGui::End();
#endif

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
        case GLFW_KEY_T: {
            static int cnt = 0;
            char buf[1024];

            snprintf(buf, 1024, "screenshot%03i", cnt);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            djgt_save_glcolorbuffer_bmp(GL_FRONT, GL_RGBA, buf);
            ++cnt;
        } break;
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

        g_viewer.camera.pos.x+= dx * sc * 2e-3;
        g_viewer.camera.pos.y-= dy * sc * 2e-3;
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
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH, VIEWPORT_HEIGHT, "Viewer", NULL, NULL);
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

