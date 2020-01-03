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

#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);

enum { TEXTURE_INPUT, TEXTURE_PAGE_RAW, TEXTURE_PAGE, TEXTURE_COUNT };

GLuint LoadInputTexture(const char *pathToFile, bool isHdr)
{
    djg_texture *djgt = djgt_create(0);
    GLuint gl;
    GLenum internalFormat = isHdr ? GL_RGBA16F : GL_RGBA8;

    glActiveTexture(GL_TEXTURE0 + TEXTURE_INPUT);
    if (isHdr) {
        djgt_push_image_hdr(djgt, pathToFile, true);
    } else {
        djgt_push_image_u8(djgt, pathToFile, true);
    }
    djgt_to_gl(djgt, GL_TEXTURE_2D, internalFormat, 1, 1, &gl);
    djgt_release(djgt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    return gl;
}

GLuint LoadPageTexture(int size, bool isHdr)
{
    GLuint texture;
    GLenum internalFormat = isHdr ? GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT
                                : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;

    glActiveTexture(GL_TEXTURE0 + TEXTURE_PAGE);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, size, size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    return texture;
}

GLuint LoadPageTextureRaw(int size, bool isHdr)
{
    GLuint texture;
    GLenum internalFormat = isHdr ? GL_RGBA16F : GL_RGBA8;

    glActiveTexture(GL_TEXTURE0 + TEXTURE_PAGE_RAW);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, size, size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    return texture;
}

GLuint LoadFramebuffer(GLuint pageTextureRaw)
{
    GLuint framebuffer;

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           pageTextureRaw,
                           0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return framebuffer;
}

GLuint LoadGenerationProgram(bool isHdr)
{
    djg_program *djgp = djgp_create();
    GLuint program;

    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/LongestEdgeBisection.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/TextureGeneration.glsl");
    djgp_to_gl(djgp, 450, false, true, &program);
    djgp_release(djgp);

    return program;
}

GLuint LoadVertexArray()
{
    GLuint vertexArray;

    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glBindVertexArray(0);

    return vertexArray;
}

void Run(int argc, char **argv)
{
    //const char *pathToFile = PATH_TO_ASSET_DIRECTORY "./debug-texture.png";
    const char *pathToFile = PATH_TO_ASSET_DIRECTORY "./kloofendal_48d_partly_cloudy_16k.hdr";
    bool isHdr = strcmp(strrchr(pathToFile, '.'), ".hdr") == 0;
    int textureRes = 16;
    int pageRes = 9;
    int texelsPerPage = 1 << (2 * pageRes);
    tt_Texture *tt;
    uint8_t *rawPageData, *pageData;
    GLuint textures[TEXTURE_COUNT];
    GLuint framebuffer, vertexArray, program;
    int bytesPerPage, bytesPerRawPage;

    // init OpenGL resources
    textures[TEXTURE_INPUT]    = LoadInputTexture(pathToFile, isHdr);
    textures[TEXTURE_PAGE]     = LoadPageTexture(1 << pageRes, isHdr);
    textures[TEXTURE_PAGE_RAW] = LoadPageTextureRaw(1 << pageRes, isHdr);
    framebuffer = LoadFramebuffer(textures[TEXTURE_PAGE_RAW]);
    vertexArray = LoadVertexArray();
    program = LoadGenerationProgram(false);

    // create the tt_Texture file
    TT_LOG("Creating %s texture", isHdr ? "HDR" : "LDR");
    tt_Create("texture.tt", isHdr ? TT_FORMAT_HDR : TT_FORMAT_RGB, textureRes, pageRes);
    tt = tt_Load("texture.tt", 256);

    // allocate data
    glGetTextureLevelParameteriv(textures[TEXTURE_PAGE], 0,
                                 GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                                 &bytesPerPage);
    pageData = (uint8_t *)malloc(bytesPerPage);
    bytesPerRawPage = texelsPerPage * /* RGBA */4 * (isHdr ? 2 : 1);
    rawPageData = (uint8_t *)malloc(bytesPerRawPage);

    // create pages and write to disk
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, 1 << pageRes, 1 << pageRes);
    glUseProgram(program);
    glBindVertexArray(vertexArray);
    for (int i = 0; i < (2 << tt->storage.depth); ++i) {
        TT_LOG("Generating page %i / %i", i + 1, (2 << tt->storage.depth));
        GLenum type = isHdr ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;

        // write to raw data
        glUniform1ui(glGetUniformLocation(program, "u_NodeID"), i);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // compress page data on the GPU
        glGetTextureImage(textures[TEXTURE_PAGE_RAW],
                          0, GL_RGBA, type, bytesPerRawPage, rawPageData);
        glTextureSubImage2D(textures[TEXTURE_PAGE],
                            0, 0, 0, 1 << pageRes, 1 << pageRes,
                            GL_RGBA, type, rawPageData);
        glGetCompressedTextureImage(textures[TEXTURE_PAGE], 0, bytesPerPage, pageData);

        // write compressed data to disk
        fseek(tt->storage.stream, sizeof(tt__Header) + (uint64_t)bytesPerPage * (uint64_t)i, SEEK_SET);
        fwrite(pageData, bytesPerPage, 1, tt->storage.stream);
    }

    // release OpenGL objects
    glDeleteTextures(TEXTURE_COUNT, textures);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(program);

    // release memory
    tt_Release(tt);
    free(pageData);
    free(rawPageData);
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
    GLFWwindow* window = glfwCreateWindow(256, 256, "Converter", NULL, NULL);
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

    glfwSwapBuffers(window);
    Run(argc, argv);
    glfwTerminate();

    return 0;
}

