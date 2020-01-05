//////////////////////////////////////////////////////////////////////////////
//
// This program creates a MegaTexture out of a displacement map.
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

#define VIEWPORT_WIDTH 1200

// default path to the directory holding the source files
#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif
#ifndef PATH_TO_ASSET_DIRECTORY
#   define PATH_TO_ASSET_DIRECTORY "../assets/"
#endif
#ifndef PATH_TO_LEB_GLSL_LIBRARY
#   define PATH_TO_LEB_GLSL_LIBRARY "./"
#endif
#ifndef PATH_TO_NOISE_GLSL_LIBRARY
#   define PATH_TO_NOISE_GLSL_LIBRARY "./"
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

enum {
    DETAIL_MAP_ROCK,
    DETAIL_MAP_GRASS,
    DETAIL_MAP_COUNT
};

struct TextureGenerator {
    struct {
        const char *pathToFile;     // path to input displacement map
        float width, height;        // size in meters
        float zMin, zMax;           // min/max altitude in meters
    } dmap;
    struct {
        const char *pathToDmap, *pathToAmap; // displacement and albedo texture maps
        float width, height;        // size in meters
        float zMin, zMax;           // min/max altitude in meters
    } detailsMaps[DETAIL_MAP_COUNT];
} g_textureGenerator = {
    {PATH_TO_ASSET_DIRECTORY "./kauai.png", 5266.0f, 5266.0f, -14.0f, 1587.0f},
    {
        {
            PATH_TO_ASSET_DIRECTORY "./rock_05_bump_1k.png",
            PATH_TO_ASSET_DIRECTORY "./ROCK-11_COLOR_4k.jpg",
            10.0f, 10.0f, 0.0f, 1.0f
        }, {
            PATH_TO_ASSET_DIRECTORY "./brown_mud_leaves_01_bump_1k.png",
            PATH_TO_ASSET_DIRECTORY "./ForestFloor-06_COLOR_4k.jpg",
            3.0f, 3.0f, 0.0f, 1.0f
        }
    }
};

struct TextureViewer {
    struct {
        struct {float x, y;} pos;
        float zoom;
    } camera;
} g_viewer = {
    { {0.0f, 0.0f}, 1.0f }
};

enum {
    TEXTURE_DMAP_TERRAIN,

    TEXTURE_DMAP_ROCK,
    TEXTURE_DMAP_GRASS,
    TEXTURE_AMAP_ROCK,
    TEXTURE_AMAP_GRASS,

    TEXTURE_DMAP_CHUNK,
    TEXTURE_AMAP_CHUNK,
    TEXTURE_NMAP_CHUNK,

    TEXTURE_COUNT
};

enum {
    PROGRAM_PREVIEW,

    PROGRAM_COUNT
};

enum {
    BUFFER_TEXTURE_DIMENSIONS,

    BUFFER_COUNT
};

struct OpenGLManager {
    GLuint vertexArray;
    GLuint buffers[BUFFER_COUNT];
    GLuint textures[TEXTURE_COUNT];
    GLuint programs[PROGRAM_COUNT];
} g_gl = {0, {0}, {0}};

// load detail maps
void LoadDetailDataTextures()
{
    glGenTextures(DETAIL_MAP_COUNT, &g_gl.textures[TEXTURE_DMAP_ROCK]);
    for (int i = 0; i < DETAIL_MAP_COUNT; ++i) {
        LOG("Loading {Dmap-Detail-Texture}\n");
        GLuint *glt = &g_gl.textures[TEXTURE_DMAP_ROCK + i];
        djg_texture *djt = djgt_create(0);
        djgt_push_image_u8(djt, g_textureGenerator.detailsMaps[i].pathToDmap, true);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_ROCK + i);
        djgt_to_gl(djt, GL_TEXTURE_2D, GL_R8, true, true, glt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        djgt_release(djt);
    }
    for (int i = 0; i < DETAIL_MAP_COUNT; ++i) {
        LOG("Loading {Amap-Detail-Texture}\n");
        GLuint *glt = &g_gl.textures[TEXTURE_AMAP_ROCK + i];
        djg_texture *djt = djgt_create(0);
        djgt_push_image_u8(djt, g_textureGenerator.detailsMaps[i].pathToAmap, true);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_AMAP_ROCK + i);
        djgt_to_gl(djt, GL_TEXTURE_2D, GL_RGBA8, true, true, glt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        djgt_release(djt);
    }
}

// load input displacement map
// this also computes the second moment of the elevation
// which we use to measure the local roughness of the terrain
void LoadTerrainDmapTexture()
{
    GLuint *glt = &g_gl.textures[TEXTURE_DMAP_TERRAIN];
    djg_texture *djgt = djgt_create(0);

    LOG("Loading {Dmap-Terrain-Texture}\n");
    djgt_push_image_u16(djgt, g_textureGenerator.dmap.pathToFile, true);

    int w = djgt->next->x;
    int h = djgt->next->y;
    const uint16_t *texels = (const uint16_t *)djgt->next->texels;
    int mipcnt = djgt__mipcnt(w, h, 1);
    std::vector<uint16_t> dmap(w * h * 2);

    for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
        uint16_t z = texels[i + w * j]; // in [0,2^16-1]
        float zf = float(z) / float((1 << 16) - 1);
        uint16_t z2 = zf * zf * ((1 << 16) - 1);

        dmap[    2 * (i + w * j)] = z;
        dmap[1 + 2 * (i + w * j)] = z2;
    }

    glGenTextures(1, glt);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_TERRAIN);
    glBindTexture(GL_TEXTURE_2D, *glt);
    glTexStorage2D(GL_TEXTURE_2D, mipcnt, GL_RG16, w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG, GL_UNSIGNED_SHORT, &dmap[0]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    djgt_release(djgt);
}

void LoadTextures()
{
    LoadDetailDataTextures();
    LoadTerrainDmapTexture();
}

void LoadPreviewProgram()
{
    LOG("Loading Preview-Program");
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_PREVIEW];
    char buf[1024];

    djgp_push_string(djp,
                     "#define WORLD_SPACE_TEXTURE_DIMENSIONS_BUFFER_BINDING %i\n",
                     BUFFER_TEXTURE_DIMENSIONS);
    djgp_push_file(djp, PATH_TO_NOISE_GLSL_LIBRARY "gpu_noise_lib.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainTexture.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainPreview.glsl"));
    djgp_to_gl(djp, 450, false, true, glp);

    glUseProgram(*glp);
    {
        GLint albedoLocations[] = {
            TEXTURE_AMAP_GRASS,
            TEXTURE_AMAP_ROCK
        };
        GLint displacementLocations[] = {
            TEXTURE_DMAP_GRASS,
            TEXTURE_DMAP_ROCK
        };
        glUniform1i(glGetUniformLocation(*glp, "TT_TerrainDisplacementSampler"),
                    TEXTURE_DMAP_TERRAIN);
        glUniform1iv(glGetUniformLocation(*glp, "TT_DetailAlbedoSamplers"),
                     DETAIL_MAP_COUNT,
                     albedoLocations);
        glUniform1iv(glGetUniformLocation(*glp, "TT_DetailDisplacementSamplers"),
                     DETAIL_MAP_COUNT,
                     displacementLocations);
    }
    glUseProgram(0);

    djgp_release(djp);
}

void LoadPrograms()
{
    LoadPreviewProgram();
}

void LoadVertexArray()
{
    glGenVertexArrays(1, &g_gl.vertexArray);
    glBindVertexArray(g_gl.vertexArray);
    glBindVertexArray(0);
}

void LoadTextureDimensionsBuffer()
{
    dja::vec4 bufferData[8];
    glGenBuffers(1, &g_gl.buffers[BUFFER_TEXTURE_DIMENSIONS]);

    bufferData[0].x = g_textureGenerator.dmap.width;
    bufferData[0].y = g_textureGenerator.dmap.height;
    bufferData[0].z = g_textureGenerator.dmap.zMin;
    bufferData[0].w = g_textureGenerator.dmap.zMax;

    for (int i = 0; i < DETAIL_MAP_COUNT; ++i) {
        bufferData[i + 1].x = g_textureGenerator.detailsMaps[i].width;
        bufferData[i + 1].y = g_textureGenerator.detailsMaps[i].height;
        bufferData[i + 1].z = g_textureGenerator.detailsMaps[i].zMin;
        bufferData[i + 1].w = g_textureGenerator.detailsMaps[i].zMax;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, g_gl.buffers[BUFFER_TEXTURE_DIMENSIONS]);
    glBufferStorage(GL_UNIFORM_BUFFER, sizeof(bufferData), bufferData, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER,
                     BUFFER_TEXTURE_DIMENSIONS,
                     g_gl.buffers[BUFFER_TEXTURE_DIMENSIONS]);
}


// -----------------------------------------------------------------------------
// allocate resources
// (typically before entering the game loop)
void Load(int /*argc*/, char **/*argv*/)
{
    LoadTextures();
    LoadVertexArray();
    LoadPrograms();
    LoadTextureDimensionsBuffer();
}

// free resources
// (typically after exiting the game loop but before context deletion)
void Release()
{
}

// -----------------------------------------------------------------------------
void Render()
{
    float zoomFactor = exp2f(-g_viewer.camera.zoom);
    float x = g_viewer.camera.pos.x;
    float y = g_viewer.camera.pos.y;
    dja::mat4 modelView = dja::mat4::homogeneous::orthographic(
        x - zoomFactor + 0.5f, x + zoomFactor + 0.5f,
        y - zoomFactor + 0.5f, y + zoomFactor + 0.5f,
        -1.0f, 1.0f
    );
    dja::mat4 projection = dja::mat4(1.0f);
    dja::mat4 mvp = dja::transpose(projection * modelView);

    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_gl.programs[PROGRAM_PREVIEW]);
    glUniformMatrix4fv(
        glGetUniformLocation(g_gl.programs[PROGRAM_PREVIEW], "u_ModelViewProjection"),
        1, GL_FALSE, &mvp[0][0]
    );
    glBindVertexArray(g_gl.vertexArray);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

// -----------------------------------------------------------------------------
void RenderGui()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0)/*, ImGuiSetCond_FirstUseEver*/);
    ImGui::SetNextWindowSize(ImVec2(256, VIEWPORT_WIDTH)/*, ImGuiSetCond_FirstUseEver*/);
    ImGui::Begin("Window");
    {
        ImGui::Text("Pos : %f %f", g_viewer.camera.pos.x, g_viewer.camera.pos.y);
        ImGui::Text("Zoom: %f", g_viewer.camera.zoom);
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// -----------------------------------------------------------------------------
void
KeyboardCallback(
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
        case GLFW_KEY_R:
            LoadPrograms();
        break;
            default: break;
        }
    }
}

void
MouseButtonCallback(GLFWwindow* /*window*/, int /*button*/, int /*action*/, int /*mods*/)
{
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

static void APIENTRY
debug_output_logger(
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

    if(severity == GL_DEBUG_SEVERITY_HIGH) {
        LOG("djg_error: %s %s\n"                \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    } else if(severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    }
}

void log_debug_output(void)
{
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&debug_output_logger, NULL);
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
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH+256, VIEWPORT_WIDTH, "Hello Imgui", NULL, NULL);
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

    LOG("-- Begin -- Demo\n");
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 450");
        log_debug_output();
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


