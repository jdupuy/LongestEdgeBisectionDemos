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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"

#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#define VIEWPORT_WIDTH 1024

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

GLuint LoadGenerationProgram()
{
    djg_program *djgp = djgp_create();
    GLuint program;

    //djgp_push_string(djgp, "#define BUFFER_BINDING_LEB 8\n");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/LongestEdgeBisection.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/TextureGeneration.glsl");
    djgp_to_gl(djgp, 450, false, true, &program);
    djgp_release(djgp);

    return program;
}


void LoadTexture(const char *pathToFile)
{
    djg_texture *djgt = djgt_create(0);
    GLuint texture;

    djgt_push_image_u8(djgt, pathToFile, true);
    djgt_to_gl(djgt, GL_TEXTURE_2D, GL_RGBA8, 1, 1, &texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    djgt_release(djgt);
}

void LoadTextureHDR(const char *pathToFile)
{
    djg_texture *djgt = djgt_create(0);
    GLuint texture;

    djgt_push_image_hdr(djgt, pathToFile, true);
    djgt_to_gl(djgt, GL_TEXTURE_2D, GL_RGBA16F, 1, 1, &texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    djgt_release(djgt);
}

void Load(int argc, char **argv, GLFWwindow *window)
{
    int textureRes = 12;
    int pageRes = 9;
    int texelsPerPage = 1 << (2 * pageRes);
    int dataByteSize  = 4 * texelsPerPage;
    tt_Texture *tt;
    uint8_t *data = (uint8_t *)TT_MALLOC(dataByteSize);

    glActiveTexture(GL_TEXTURE0);
    GLuint pageTexture;
    glGenTextures(1, &pageTexture);
    glBindTexture(GL_TEXTURE_2D, pageTexture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                 1 << pageRes, 1 << pageRes,
                 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glActiveTexture(GL_TEXTURE1);
    GLuint pageTextureData;
    glGenTextures(1, &pageTextureData);
    glBindTexture(GL_TEXTURE_2D, pageTextureData);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 1 << pageRes, 1 << pageRes,
                 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE2);
    LoadTexture(PATH_TO_ASSET_DIRECTORY "./gtav-map-satellite-huge.png");
    glActiveTexture(GL_TEXTURE0);

    GLuint program = LoadGenerationProgram();
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_InputSampler"), 2);
    glUseProgram(0);

    GLuint vertexArray;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glBindVertexArray(0);

    // create the tt_Texture file
    tt_Create("testRGB.tt", TT_FORMAT_RGB, textureRes, pageRes);
    tt = tt_Load("testRGB.tt", 256);

    GLint pageByteSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                             GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                             &pageByteSize);
    TT_LOG("compressed Byte size: %i (%i)", pageByteSize, texelsPerPage / 2);

    // create pages and write to disk
    for (int i = 0; i < (2 << tt->storage.depth); ++i) {
        TT_LOG("Generating page %i / %i", i + 1, (2 << tt->storage.depth));
#if 0
        srand(i);
        uint8_t r = rand() & 255u, g = rand() & 255u, b = rand() & 255u;
        for (int i = 0; i < 1 << (2 * tt->storage.pages.size); ++i) {
            data[4 * i    ] = r;
            data[4 * i + 1] = g;
            data[4 * i + 2] = b;
            data[4 * i + 3] = 255u;
        }

        // updload image
        glTextureSubImage2D(pageTexture, 0, 0, 0, 1 << pageRes, 1 << pageRes,
                            GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
        glBindImageTexture(0, pageTextureData, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        glViewport(0, 0, 1 << pageRes, 1 << pageRes);
        glUseProgram(program);
        glUniform1ui(glGetUniformLocation(program, "u_NodeID"), i);
        glBindVertexArray(vertexArray);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        //glfwSwapBuffers(window);

        // transfer data to compressed texture
        glGetTextureImage(pageTextureData, 0, GL_RGBA, GL_UNSIGNED_BYTE, dataByteSize, (void *)data);
        glTextureSubImage2D(pageTexture, 0, 0, 0, 1 << pageRes, 1 << pageRes,
                            GL_RGBA, GL_UNSIGNED_BYTE, data);
#endif
        // retrieve data
        glGetCompressedTexImage(GL_TEXTURE_2D, 0, data);

        fseek(tt->storage.stream, sizeof(tt__Header) + pageByteSize * i, SEEK_SET);
        fwrite(data, pageByteSize, 1, tt->storage.stream);
    }

    glDeleteTextures(1, &pageTexture);

    tt_Release(tt);
    TT_FREE(data);
}

void LoadHDR(int argc, char **argv, GLFWwindow *window)
{
    int textureRes = 14;
    int pageRes = 9;
    int texelsPerPage = 1 << (2 * pageRes);
    int dataByteSize  = 4 * 4 * texelsPerPage;
    tt_Texture *tt;
    float *data = (float *)TT_MALLOC(dataByteSize);

    glActiveTexture(GL_TEXTURE0);
    GLuint pageTexture;
    glGenTextures(1, &pageTexture);
    glBindTexture(GL_TEXTURE_2D, pageTexture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT,
                 1 << pageRes, 1 << pageRes,
                 0,
                 GL_RGB, GL_UNSIGNED_BYTE, NULL);

    glActiveTexture(GL_TEXTURE1);
    GLuint pageTextureData;
    glGenTextures(1, &pageTextureData);
    glBindTexture(GL_TEXTURE_2D, pageTextureData);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA32F,
                 1 << pageRes, 1 << pageRes,
                 0,
                 GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE2);
    LoadTextureHDR(PATH_TO_ASSET_DIRECTORY "./kloofendal_16k.hdr");
    glActiveTexture(GL_TEXTURE0);

    GLuint program = LoadGenerationProgram();
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_InputSampler"), 2);
    glUseProgram(0);

    GLuint vertexArray;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glBindVertexArray(0);

    // create the tt_Texture file
    tt_Create("testHDR.tt", TT_FORMAT_HDR, textureRes, pageRes);
    tt = tt_Load("testHDR.tt", 256);

    GLint pageByteSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                             GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                             &pageByteSize);
    TT_LOG("=> Compressed Byte size: %i (%i)", pageByteSize, texelsPerPage);

    // create pages and write to disk
    for (int i = 0; i < (2 << tt->storage.depth); ++i) {
        TT_LOG("Generating page %i / %i", i + 1, (2 << tt->storage.depth));
#if 0
        srand(i);
        uint8_t r = rand() & 255u, g = rand() & 255u, b = rand() & 255u;
        for (int i = 0; i < 1 << (2 * tt->storage.pages.size); ++i) {
            data[4 * i    ] = r;
            data[4 * i + 1] = g;
            data[4 * i + 2] = b;
            data[4 * i + 3] = 255u;
        }

        // updload image
        glTextureSubImage2D(pageTexture, 0, 0, 0, 1 << pageRes, 1 << pageRes,
                            GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
        glBindImageTexture(0, pageTextureData, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glViewport(0, 0, 1 << pageRes, 1 << pageRes);
        glUseProgram(program);
        glUniform1ui(glGetUniformLocation(program, "u_NodeID"), i);
        glBindVertexArray(vertexArray);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glfwSwapBuffers(window);

        // transfer data to compressed texture
        glGetTextureImage(pageTextureData, 0, GL_RGBA, GL_FLOAT, dataByteSize, (void *)data);
        glTextureSubImage2D(pageTexture, 0, 0, 0, 1 << pageRes, 1 << pageRes,
                            GL_RGBA, GL_FLOAT, data);
#endif
        // retrieve data
        glGetCompressedTexImage(GL_TEXTURE_2D, 0, data);

        fseek(tt->storage.stream, sizeof(tt__Header) + pageByteSize * i, SEEK_SET);
        fwrite(data, pageByteSize, 1, tt->storage.stream);
    }

    glDeleteTextures(1, &pageTexture);

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
        //Load(argc, argv, window);
        LoadHDR(argc, argv, window);
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

