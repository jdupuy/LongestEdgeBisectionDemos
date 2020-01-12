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

enum {
    TEXTURE_PAGE_CHILDREN,
    TEXTURE_PAGE_RAW,
    TEXTURE_PAGE,

    TEXTURE_COUNT
};

typedef struct {
    int64_t bytesPerTexel;
    GLenum format, type, internalFormat;
} RawTextureStorage;

RawTextureStorage GenRawTextureStorage(tt_Format format)
{
    RawTextureStorage storage;

    switch (format) {
    // uint8
    case TT_FORMAT_R8:
        storage.format = GL_RED;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_R8;
        storage.bytesPerTexel = 1;
        break;
    case TT_FORMAT_RG8:
        storage.format = GL_RG;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_RG8;
        storage.bytesPerTexel = 2;
        break;
    case TT_FORMAT_RGBA8:
        storage.format = GL_RGBA;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_RGBA8;
        storage.bytesPerTexel = 4;
        break;
    // uint16
    case TT_FORMAT_R16:
        storage.format = GL_RED;
        storage.type   = GL_UNSIGNED_SHORT;
        storage.internalFormat = GL_R16;
        storage.bytesPerTexel = 2;
        break;
    case TT_FORMAT_RG16:
        storage.format = GL_RG;
        storage.type   = GL_UNSIGNED_SHORT;
        storage.internalFormat = GL_RG16;
        storage.bytesPerTexel = 4;
        break;
    case TT_FORMAT_RGBA16:
        storage.format = GL_RGBA;
        storage.type   = GL_UNSIGNED_SHORT;
        storage.internalFormat = GL_RGBA16;
        storage.bytesPerTexel = 8;
        break;
    // f16
    case TT_FORMAT_R16F:
        storage.format = GL_RED;
        storage.type   = GL_HALF_FLOAT;
        storage.internalFormat = GL_R16F;
        storage.bytesPerTexel = 2;
        break;
    case TT_FORMAT_RG16F:
        storage.format = GL_RG;
        storage.type   = GL_HALF_FLOAT;
        storage.internalFormat = GL_RG16F;
        storage.bytesPerTexel = 4;
        break;
    case TT_FORMAT_RGBA16F:
        storage.format = GL_RGBA;
        storage.type   = GL_HALF_FLOAT;
        storage.internalFormat = GL_RGBA16F;
        storage.bytesPerTexel = 8;
        break;
    // f32
    case TT_FORMAT_R32F:
        storage.format = GL_RED;
        storage.type   = GL_FLOAT;
        storage.internalFormat = GL_R32F;
        storage.bytesPerTexel = 4;
        break;
    case TT_FORMAT_RG32F:
        storage.format = GL_RG;
        storage.type   = GL_FLOAT;
        storage.internalFormat = GL_RG32F;
        storage.bytesPerTexel = 8;
        break;
    case TT_FORMAT_RGBA32F:
        storage.format = GL_RGBA;
        storage.type   = GL_FLOAT;
        storage.internalFormat = GL_RGBA32F;
        storage.bytesPerTexel = 16;
        break;
    // Compressed formats
    case TT_FORMAT_BC1:
    case TT_FORMAT_BC1_ALPHA:
    case TT_FORMAT_BC2:
    case TT_FORMAT_BC3:
        storage.format = GL_RGBA;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_RGBA8;
        storage.bytesPerTexel = 4;
        break;
    case TT_FORMAT_BC4:
        storage.format = GL_RED;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_R8;
        storage.bytesPerTexel = 1;
        break;
    case TT_FORMAT_BC5:
        storage.format = GL_RG;
        storage.type   = GL_UNSIGNED_BYTE;
        storage.internalFormat = GL_RG8;
        storage.bytesPerTexel = 2;
        break;
    case TT_FORMAT_BC6:
    case TT_FORMAT_BC6_SIGNED:
    case TT_FORMAT_BC7:
    case TT_FORMAT_BC7_SRGB:
        storage.format = GL_RGBA;
        storage.type   = GL_HALF_FLOAT;
        storage.internalFormat = GL_RGBA16F;
        storage.bytesPerTexel = 4;
        break;
    }

    return storage;
}

GLuint LoadTexture(int textureID, GLenum internalFormat, int size)
{
    GLuint texture;

    glActiveTexture(GL_TEXTURE0 + textureID);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, 1 << size, 1 << size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    return texture;
}

GLuint LoadChildrenTexture(int textureID, GLenum internalFormat, int size)
{
    GLuint texture;

    glActiveTexture(GL_TEXTURE0 + textureID);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, size + 1, internalFormat, 1 << size, 1 << size, 2);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
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

GLuint LoadGenerationProgram()
{
    djg_program *djgp = djgp_create();
    GLuint program;

    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/LongestEdgeBisection.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/MipmapGeneration.glsl");
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
    int64_t pageTextureByteOffset = 0;

    // load texture
    tt_Texture *tt = tt_Load("texture.tt", 16);
    // MIPmap texture
    int depth = tt->storage.header.depth - 1;

    GLuint vertexArray = LoadVertexArray();
    GLuint program = LoadGenerationProgram();

    glBindVertexArray(vertexArray);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_ChildrenSampler"), TEXTURE_PAGE_CHILDREN);

    TT_LOG("Mipmapping %li textures", tt_TexturesPerPage(tt));
    for (int textureID = 0; textureID < tt_TexturesPerPage(tt); ++textureID) {
        int64_t textureSize     = tt_PageTextureSize(tt, textureID);
        tt_Format textureFormat = tt_PageTextureFormat(tt, textureID);
        RawTextureStorage storage = GenRawTextureStorage(textureFormat);
        int64_t bytesPerPage        = tt_BytesPerPage(tt);
        int64_t bytesPerPageTexture = tt_BytesPerPageTexture(tt, textureID);
        int64_t bytesPerPageTextureRaw = storage.bytesPerTexel * (1 << (2 * textureSize));
        uint8_t *pageData       = (uint8_t *)malloc(2 * bytesPerPage);
        uint8_t *textureData    = (uint8_t *)malloc(bytesPerPageTexture);
        uint8_t *textureRawData = (uint8_t *)malloc(bytesPerPageTextureRaw);
        GLuint textures[TEXTURE_COUNT], framebuffer;


        // load textures
        textures[TEXTURE_PAGE_CHILDREN] = LoadChildrenTexture(TEXTURE_PAGE_CHILDREN,
                                                              tt__PageTextureInternalFormat(tt, textureID),
                                                              textureSize);
        textures[TEXTURE_PAGE] = LoadTexture(TEXTURE_PAGE,
                                             tt__PageTextureInternalFormat(tt, textureID),
                                             textureSize);
        textures[TEXTURE_PAGE_RAW] = LoadTexture(TEXTURE_PAGE_RAW,
                                                 storage.internalFormat,
                                                 textureSize);
        // load framebuffer
        framebuffer = LoadFramebuffer(textures[TEXTURE_PAGE_RAW]);

        // set GL state
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, 1 << textureSize, 1 << textureSize);

        // MIP depth
        for (int64_t d = depth - 1; d >= 0; --d) {
            TT_LOG("Processing MIP level %li", d);
            int64_t minNodeID = 1 << d;
            int64_t maxNodeID = 2 << d;

            // LEB nodes
            for (int64_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
                // load left and right child page data
                fseek(tt->storage.stream,
                      sizeof(tt__Header) + 2 * nodeID * bytesPerPage,
                      SEEK_SET);
                fread(pageData, bytesPerPage, 2, tt->storage.stream);

                // upload page data to GPU
                if (textureFormat >= TT_FORMAT_BC1) {
                    for (int i = 0; i < 2; ++i) {
                        glCompressedTextureSubImage3D(textures[TEXTURE_PAGE_CHILDREN],
                                                      0,
                                                      0, 0, i,
                                                      1 << textureSize,
                                                      1 << textureSize,
                                                      1,
                                                      tt__PageTextureInternalFormat(tt, textureID),
                                                      bytesPerPageTexture,
                                                      &pageData[pageTextureByteOffset + i * bytesPerPage]);
                    }
                } else {
                    GLenum format, type;

                    switch (textureFormat) {
                    case TT_FORMAT_R8: format = GL_RED; type = GL_UNSIGNED_BYTE; break;
                    case TT_FORMAT_R16: format = GL_RED; type = GL_UNSIGNED_SHORT; break;
                    case TT_FORMAT_R16F: format = GL_RED; type = GL_HALF_FLOAT; break;
                    case TT_FORMAT_R32F: format = GL_RED; type = GL_FLOAT; break;
                    case TT_FORMAT_RG8: format = GL_RG; type = GL_UNSIGNED_BYTE; break;
                    case TT_FORMAT_RG16: format = GL_RG; type = GL_UNSIGNED_SHORT; break;
                    case TT_FORMAT_RG16F: format = GL_RG; type = GL_HALF_FLOAT; break;
                    case TT_FORMAT_RG32F: format = GL_RG; type = GL_FLOAT; break;
                    case TT_FORMAT_RGBA8: format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
                    case TT_FORMAT_RGBA16: format = GL_RGBA; type = GL_UNSIGNED_SHORT; break;
                    case TT_FORMAT_RGBA16F: format = GL_RGBA; type = GL_HALF_FLOAT; break;
                    case TT_FORMAT_RGBA32F: format = GL_RGBA; type = GL_FLOAT; break;
                    default: break;
                    }

                    for (int i = 0; i < 2; ++i) {
                        glTextureSubImage3D(textures[TEXTURE_PAGE_CHILDREN],
                                            0,
                                            0, 0, i,
                                            1 << textureSize,
                                            1 << textureSize,
                                            1,
                                            format,
                                            type,
                                            &pageData[pageTextureByteOffset + i * bytesPerPage]);
                    }
                }

                glGenerateTextureMipmap(textures[TEXTURE_PAGE_CHILDREN]);

                // execute Kernel
                glUniform1ui(glGetUniformLocation(program, "u_NodeID"), nodeID);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // compress page data on the GPU
                glGetTextureImage(textures[TEXTURE_PAGE_RAW],
                                  0, storage.format, storage.type,
                                  bytesPerPageTextureRaw, textureRawData);
                glTextureSubImage2D(textures[TEXTURE_PAGE],
                                    0, 0, 0, 1 << textureSize, 1 << textureSize,
                                    storage.format, storage.type, textureRawData);
                glGetCompressedTextureImage(textures[TEXTURE_PAGE], 0, bytesPerPageTexture, textureData);

                // write compressed data to disk
                fseek(tt->storage.stream,
                      sizeof(tt__Header) + (uint64_t)bytesPerPage * nodeID + pageTextureByteOffset,
                      SEEK_SET);
                fwrite(textureData, bytesPerPageTexture, 1, tt->storage.stream);
            }
        }

        pageTextureByteOffset+= bytesPerPageTexture;
        TT_LOG("%li (%li)", tt_BytesPerPageTexture(tt, 0) + tt_BytesPerPageTexture(tt, 1), bytesPerPage);

        // cleanup
        glDeleteTextures(TEXTURE_COUNT, textures);
        glDeleteFramebuffers(1, &framebuffer);
        free(pageData);
        free(textureData);
        free(textureRawData);
    }

    // cleanup
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(program);

    // release memory
    tt_Release(tt);
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

    log_debug_output();
    glfwSwapBuffers(window);
    Run(argc, argv);
    glfwTerminate();

    return 0;
}

