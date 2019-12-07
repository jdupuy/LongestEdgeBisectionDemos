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

#define VIEWPORT_WIDTH 800

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
        int resolution;             // resolution
        float zMin, zMax;           // min/max altitude in meters
        float size;                 // size in meters
    } dmap;
    struct {
        const char *pathToDmap, *pathToAmap; // displacement and albedo texture maps
        int resolution;             // texture resolution
        float zMax;                 // max height in meters
        float size;                 // data spatial footprint
    } detailsMaps[DETAIL_MAP_COUNT];
    int targetResolution;           // side resolution of the texture to generate
    int chunkResolution;            // side resolution of each chunk textures
} g_textureGenerator = {
    {PATH_TO_ASSET_DIRECTORY "./kauai.png", -1, -14.0f, 1587.0f, 5266.0f},
    {
        {
            PATH_TO_ASSET_DIRECTORY "./rock_05_bump_1k.png",
            PATH_TO_ASSET_DIRECTORY "./rock_05_diff_1k.png",
            -1, 0.010f, 1.0f
        }, {
            PATH_TO_ASSET_DIRECTORY "./brown_mud_leaves_01_bump_1k.png",
            PATH_TO_ASSET_DIRECTORY "./brown_mud_leaves_01_diff_1k.png",
            -1, 0.010f, 4.0f
        }
    },
    1024 * 1024,
    1024
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
    PROGRAM_CHUNK_GENERATOR,
    PROGRAM_CHUNK_PREVIEW,
    PROGRAM_COUNT
};

struct OpenGLManager {
    GLuint vertexArray;
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
        g_textureGenerator.detailsMaps[i].resolution = djt->next->x;
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
void LoadTerrainDmapTexture()
{
    GLuint *glt = &g_gl.textures[TEXTURE_DMAP_TERRAIN];
    djg_texture *djt = djgt_create(0);

    LOG("Loading {Dmap-Terrain-Texture}\n");
    djgt_push_image_u16(djt, g_textureGenerator.dmap.pathToFile, true);
    glGenTextures(1, &g_gl.textures[TEXTURE_DMAP_TERRAIN]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_TERRAIN);
    djgt_to_gl(djt, GL_TEXTURE_2D, GL_R16, true, true, glt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    g_textureGenerator.dmap.resolution = djt->next->x;
    djgt_release(djt);
}

// chunk data textures
void LoadChunkDataTextures()
{
    const int textureSize = g_textureGenerator.chunkResolution;

    // displacement is a 16bit single channel texture
    LOG("Loading {Dmap-Chunk-Texture}\n");
    glGenTextures(1, &g_gl.textures[TEXTURE_DMAP_CHUNK]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_CHUNK);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_DMAP_CHUNK]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16, textureSize, textureSize);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // albedo is a compressed RGB texture
    LOG("Loading {Amap-Chunk-Texture}\n");
    glGenTextures(1, &g_gl.textures[TEXTURE_AMAP_CHUNK]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_AMAP_CHUNK);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_AMAP_CHUNK]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, textureSize, textureSize);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // normal is a compressed RG texture
    LOG("Loading {Nmap-Chunk-Texture}\n");
    glGenTextures(1, &g_gl.textures[TEXTURE_NMAP_CHUNK]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_NMAP_CHUNK);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_NMAP_CHUNK]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RG8, textureSize, textureSize);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

void LoadTextures()
{
    LoadDetailDataTextures();
    LoadTerrainDmapTexture();
    LoadChunkDataTextures();
}

void LoadChunkGeneratorProgram()
{
    LOG("Loading Chunk-Generator-Program");
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_CHUNK_GENERATOR];
    char buf[1024];

    djgp_push_file(djp, PATH_TO_NOISE_GLSL_LIBRARY "gpu_noise_lib.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ChunkGenerator.glsl"));
    djgp_to_gl(djp, 450, false, true, glp);

    glUseProgram(*glp);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkDmapSampler"), TEXTURE_DMAP_CHUNK);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkAmapSampler"), TEXTURE_AMAP_CHUNK);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkNmapSampler"), TEXTURE_NMAP_CHUNK);
    glUniform1i(glGetUniformLocation(*glp, "u_RockDmapSampler"), TEXTURE_DMAP_ROCK);
    glUniform1i(glGetUniformLocation(*glp, "u_RockAmapSampler"), TEXTURE_AMAP_ROCK);
    glUniform1i(glGetUniformLocation(*glp, "u_GrassDmapSampler"), TEXTURE_DMAP_GRASS);
    glUniform1i(glGetUniformLocation(*glp, "u_GrassAmapSampler"), TEXTURE_AMAP_GRASS);
    glUniform1i(glGetUniformLocation(*glp, "u_TerrainDmapSampler"), TEXTURE_DMAP_TERRAIN);

    glUniform1i(glGetUniformLocation(*glp, "u_TerrainDmapResolution"),
                g_textureGenerator.dmap.resolution);
    glUniform2f(glGetUniformLocation(*glp, "u_TerrainDmapZminZmax"),
                g_textureGenerator.dmap.zMin, g_textureGenerator.dmap.zMax);
    glUniform1f(glGetUniformLocation(*glp, "u_TerrainDmapSize"),
                g_textureGenerator.dmap.size);

    glUniform1i(glGetUniformLocation(g_gl.programs[PROGRAM_CHUNK_GENERATOR],
                                     "u_ChunkResolution"),
                g_textureGenerator.chunkResolution);
    glUniform1i(glGetUniformLocation(*glp, "u_MegaTextureResolution"),
                g_textureGenerator.targetResolution);

    // frequencies
    glUniform1f(glGetUniformLocation(*glp, "u_GrassFrequency"),
                g_textureGenerator.dmap.size /
                g_textureGenerator.detailsMaps[DETAIL_MAP_GRASS].size);
    glUniform1f(glGetUniformLocation(*glp, "u_RockFrequency"),
                g_textureGenerator.dmap.size /
                g_textureGenerator.detailsMaps[DETAIL_MAP_ROCK].size);

    glUseProgram(0);

    djgp_release(djp);
}

void LoadChunkPreviewProgram()
{
    LOG("Loading Chunk-Preview-Program");
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_CHUNK_PREVIEW];
    char buf[1024];

    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ChunkPreview.glsl"));
    djgp_to_gl(djp, 450, false, true, glp);

    glUseProgram(*glp);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkDmapSampler"), TEXTURE_DMAP_CHUNK);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkAmapSampler"), TEXTURE_AMAP_CHUNK);
    glUniform1i(glGetUniformLocation(*glp, "u_ChunkNmapSampler"), TEXTURE_NMAP_CHUNK);
    glUseProgram(0);

    djgp_release(djp);
}

void LoadPrograms()
{
    LoadChunkGeneratorProgram();
    LoadChunkPreviewProgram();
}

void LoadVertexArray()
{
    glGenVertexArrays(1, &g_gl.vertexArray);
    glBindVertexArray(g_gl.vertexArray);
    glBindVertexArray(0);
}

// -----------------------------------------------------------------------------
// allocate resources
// (typically before entering the game loop)
void Load(int /*argc*/, char **/*argv*/)
{
    LoadTextures();
    LoadVertexArray();
    LoadPrograms();
}

// free resources
// (typically after exiting the game loop but before context deletion)
void Release()
{
}

// -----------------------------------------------------------------------------
void GenerateChunk(int x, int y, int zoom)
{
    int globalSize = g_textureGenerator.chunkResolution;
    int localSize = 32;
    int numGroup = globalSize / localSize;
    int mag = 1 << zoom;

    LoadChunkGeneratorProgram();
    glBindImageTexture(TEXTURE_DMAP_CHUNK,
                       g_gl.textures[TEXTURE_DMAP_CHUNK],
                       0,
                       GL_FALSE,
                       0,
                       GL_WRITE_ONLY,
                       GL_R16);
    glBindImageTexture(TEXTURE_AMAP_CHUNK,
                       g_gl.textures[TEXTURE_AMAP_CHUNK],
                       0,
                       GL_FALSE,
                       0,
                       GL_WRITE_ONLY,
                       GL_RGBA8);
    glBindImageTexture(TEXTURE_NMAP_CHUNK,
                       g_gl.textures[TEXTURE_NMAP_CHUNK],
                       0,
                       GL_FALSE,
                       0,
                       GL_WRITE_ONLY,
                       GL_RG8);

    glUseProgram(g_gl.programs[PROGRAM_CHUNK_GENERATOR]);

    glUniform1i(glGetUniformLocation(g_gl.programs[PROGRAM_CHUNK_GENERATOR],
                                     "u_ChunkZoomFactor"),
                g_textureGenerator.targetResolution / (mag * g_textureGenerator.chunkResolution));
    glUniform2i(glGetUniformLocation(g_gl.programs[PROGRAM_CHUNK_GENERATOR],
                                     "u_ChunkCoordinate"),
                x * g_textureGenerator.targetResolution / mag,
                y * g_textureGenerator.targetResolution / mag);

    glDispatchCompute(numGroup, numGroup, 1);
    glUseProgram(0);

    glBindImageTexture(TEXTURE_DMAP_CHUNK, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16);
    glBindImageTexture(TEXTURE_AMAP_CHUNK, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindImageTexture(TEXTURE_NMAP_CHUNK, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG8);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

// -----------------------------------------------------------------------------
void RenderChunk()
{
    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    glUseProgram(g_gl.programs[PROGRAM_CHUNK_PREVIEW]);
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
        int chunkToTargetRatio = g_textureGenerator.targetResolution
                               / g_textureGenerator.chunkResolution;
        static int zoom = 0;
        static int x = 0, y = 0;
        if (ImGui::Button("GenerateChunk")) {
            GenerateChunk(x, y, zoom);
        }
        if (ImGui::SliderInt("Zoom", &zoom, 0, 10)) {
            GenerateChunk(x, y, zoom);
        }
        if (ImGui::SliderInt("XPos", &x, 0, (1 << zoom) - 1)) {
            GenerateChunk(x, y, zoom);
        }
        if (ImGui::SliderInt("YPos", &y, 0, (1 << zoom) - 1)) {
            GenerateChunk(x, y, zoom);
        }
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

void MouseMotionCallback(GLFWwindow* /*window*/, double x, double y)
{
    static double x0 = 0, y0 = 0;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

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

            RenderChunk();
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


