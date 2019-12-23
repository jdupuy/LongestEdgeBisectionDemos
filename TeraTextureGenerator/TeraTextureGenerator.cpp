/////////////////////////////////////////////////////////////////////////////
//
// Longest Edge Bisection (LEB) Subdivision Demo
//
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <stdexcept>
#include <vector>

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
    } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    }
}

void SetupDebugOutput(void)
{
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&DebugOutputLogger, NULL);
}

void Load(int argc, char **argv)
{
    int textureRes = 12;
    int pageRes = 8;
    int texelsPerPage = 1 << (2 * pageRes);
    int dataByteSize  = 4 * texelsPerPage;
    tt_Texture *tt;
    uint8_t *data = (uint8_t *)TT_MALLOC(dataByteSize);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                 1 << pageRes, 1 << pageRes,
                 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // create the texture
    tt_Create("testRGB.tt", TT_FORMAT_RGB, textureRes, pageRes);
    tt = tt_Load("testRGB.tt", 256);

    GLint pageByteSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                             GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                             &pageByteSize);
    TT_LOG("compressed Byte size: %i (%i)", pageByteSize, texelsPerPage / 2);

    // create pages and write to disk
    for (int i = 0; i < (2 << tt->storage.depth); ++i) {
        TT_LOG("Producing page %i / %i", i + 1, (2 << tt->storage.depth));

        srand(i);
        uint8_t r = rand() & 255u, g = rand() & 255u, b = rand() & 255u;
        for (int i = 0; i < 1 << (2 * tt->storage.pages.size); ++i) {
            data[4 * i    ] = r;
            data[4 * i + 1] = g;
            data[4 * i + 2] = b;
            data[4 * i + 3] = 255u;
        }

        // updload image
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1 << pageRes, 1 << pageRes,
                        GL_RGBA, GL_UNSIGNED_BYTE, data);
        // retrieve data
        glGetCompressedTexImage(GL_TEXTURE_2D, 0, data);

        fseek(tt->storage.stream, sizeof(tt__Header) + pageByteSize * i, SEEK_SET);
        fwrite(data, pageByteSize, 1, tt->storage.stream);
    }

    glDeleteTextures(1, &texture);

    tt_Release(tt);
    TT_FREE(data);
}

void Release()
{

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

    // Load OpenGL functions
    LOG("Loading {OpenGL}\n");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("gladLoadGLLoader failed\n");
        return -1;
    }

    SetupDebugOutput();

    LOG("-- Begin -- Demo\n");
    try {
        Load(argc, argv);
        glfwTerminate();
    } catch (std::exception& e) {
        LOG("%s", e.what());
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    } catch (...) {
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    LOG("-- End -- Demo\n");

    return 0;
}

