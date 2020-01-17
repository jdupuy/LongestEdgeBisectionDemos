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

#define LOG(fmt, ...)  fprintf(stdout, fmt "\n", ##__VA_ARGS__); fflush(stdout);

#ifndef M_PI
#define M_PI 3.141592654
#endif
#define BUFFER_SIZE(x)    ((int)(sizeof(x)/sizeof(x[0])))
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

// -----------------------------------------------------------------------------
struct AppManager {
    struct {
        const char *shader;
        const char *output;
    } dir;
    struct {
        int on, frame, capture;
    } recorder;
    int frameID, frameCount;
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

enum {
    TONEMAP_UNCHARTED2,
    TONEMAP_FILMIC,
    TONEMAP_ACES,
    TONEMAP_REINHARD,
    TONEMAP_RAW
};

struct DemoData {
    struct {
        const char *path;
    } image;
    struct {
        int depth;
        float targetStdev;
    } leb;
    struct {
        int active;
        int count;
    } samples;
    struct {
        struct {float x, y;} pos;
        float zoom;
        int tonemap;
    } camera;
    struct {bool freezeLeb, showLeb;} flags;
} g_demo = {
    {PATH_TO_ASSET_DIRECTORY "./kloofendal_48d_partly_cloudy_1k.hdr"},
    {
        20, 1.0f
    },
    {
        1, 8192
    },
    {
        {0.0f, 0.0f},
        0.75f,
        TONEMAP_FILMIC
    }, {false, true}
};

enum {
    PROGRAM_LEB_MERGE,
    PROGRAM_LEB_SPLIT,
    PROGRAM_LEB_RENDER,
    PROGRAM_LEB_BATCH,
    PROGRAM_LEB_REDUCTION_PREPASS,
    PROGRAM_LEB_REDUCTION,
    PROGRAM_LEB_SAMPLING,

    PROGRAM_COUNT
};
enum { STREAM_LEB_VARIABLES, STREAM_COUNT };
enum { VERTEXARRAY_EMPTY, VERTEXARRAY_SAMPLING, VERTEXARRAY_COUNT };
enum {
    BUFFER_LEB_HEAP,
    BUFFER_LEB_DRAW_ARRAYS,
    BUFFER_LEB_DISPATCH,
    BUFFER_RANDOM,

    BUFFER_COUNT
};
enum {
    TEXTURE_IMAGE,
    TEXTURE_DENSITY,

    TEXTURE_COUNT
};
enum {
    UNIFORM_LEB_UPDATE_DENSITY_SAMPLER,
    UNIFORM_LEB_UPDATE_TARGET_VARIANCE,

    UNIFORM_LEB_RENDER_FRAMEBUFFER_RESOLUTION,
    UNIFORM_LEB_RENDER_IMAGE_SAMPLER,
    UNIFORM_LEB_RENDER_DENSITY_SAMPLER,
    UNIFORM_LEB_RENDER_MVP_MATRIX,

    UNIFORM_LEB_SAMPLING_MVP_MATRIX,

    UNIFORM_COUNT
};

struct OpenGLManager {
    GLuint buffers[BUFFER_COUNT];
    GLuint vertexArrays[VERTEXARRAY_COUNT];
    GLuint programs[PROGRAM_COUNT];
    GLuint textures[TEXTURE_COUNT];
    GLint uniforms[UNIFORM_COUNT];
} g_gl = {0, 0};

// -----------------------------------------------------------------------------

char *strcat2(char *dst, const char *src1, const char *src2)
{
    strcpy(dst, src1);

    return strcat(dst, src2);
}

////////////////////////////////////////////////////////////////////////////////
// Program Configuration
//
////////////////////////////////////////////////////////////////////////////////

void ConfigureLebUpdateProgram(GLuint program)
{
    glProgramUniform1i(program,
                       g_gl.uniforms[UNIFORM_LEB_UPDATE_DENSITY_SAMPLER],
                       TEXTURE_DENSITY);
    glProgramUniform1f(program,
                       g_gl.uniforms[UNIFORM_LEB_UPDATE_TARGET_VARIANCE],
                       g_demo.leb.targetStdev * g_demo.leb.targetStdev);
}

void ConfigureLebUpdatePrograms()
{
    ConfigureLebUpdateProgram(g_gl.programs[PROGRAM_LEB_MERGE]);
    ConfigureLebUpdateProgram(g_gl.programs[PROGRAM_LEB_SPLIT]);
}

void ConfigureLebRenderProgram(GLuint program)
{
    glProgramUniform1i(program,
                       g_gl.uniforms[UNIFORM_LEB_RENDER_IMAGE_SAMPLER],
                       TEXTURE_IMAGE);
    glProgramUniform1i(program,
                       g_gl.uniforms[UNIFORM_LEB_RENDER_DENSITY_SAMPLER],
                       TEXTURE_DENSITY);
    glProgramUniform2f(program,
                       g_gl.uniforms[UNIFORM_LEB_RENDER_FRAMEBUFFER_RESOLUTION],
                       VIEWPORT_WIDTH,
                       VIEWPORT_WIDTH);
}


////////////////////////////////////////////////////////////////////////////////
// Program Loading
//
////////////////////////////////////////////////////////////////////////////////

void AppendLebHeader(djg_program *djp)
{
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB_HEAP);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/Leb.glsl");
}

bool LoadLebUpdateProgram(GLuint *program, const char *options)
{
    djg_program *djp = djgp_create();

    djgp_push_string(djp, options);
    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebUpdate.glsl");
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    g_gl.uniforms[UNIFORM_LEB_UPDATE_DENSITY_SAMPLER] =
        glGetUniformLocation(*program, "u_DensitySampler");
    g_gl.uniforms[UNIFORM_LEB_UPDATE_TARGET_VARIANCE] =
        glGetUniformLocation(*program, "u_TargetVariance");

    ConfigureLebUpdateProgram(*program);

    djgp_release(djp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebMergeProgram()
{
    LOG("Loading {Leb-Merge-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_MERGE];

    return LoadLebUpdateProgram(program, "#define FLAG_MERGE 1\n");
}

bool LoadLebSplitProgram()
{
    LOG("Loading {Leb-Split-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_SPLIT];

    return LoadLebUpdateProgram(program, "#define FLAG_SPLIT 1\n");
}

bool LoadLebRenderProgram()
{
    LOG("Loading {Leb-Render-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_RENDER];
    djg_program *djp = djgp_create();

    if(g_demo.flags.showLeb) {
        djgp_push_string(djp, "#define FLAG_WIRE 1\n");
    }
    switch (g_demo.camera.tonemap) {
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
    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/ToneMapping.glsl");
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebRender.glsl");

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    g_gl.uniforms[UNIFORM_LEB_RENDER_FRAMEBUFFER_RESOLUTION] =
        glGetUniformLocation(*program, "u_FramebufferResolution");
    g_gl.uniforms[UNIFORM_LEB_RENDER_IMAGE_SAMPLER] =
        glGetUniformLocation(*program, "u_ImageSampler");
    g_gl.uniforms[UNIFORM_LEB_RENDER_DENSITY_SAMPLER] =
        glGetUniformLocation(*program, "u_DensitySampler");
    g_gl.uniforms[UNIFORM_LEB_RENDER_MVP_MATRIX] =
        glGetUniformLocation(*program, "u_ModelViewProjectionMatrix");

    ConfigureLebRenderProgram(*program);
    djgp_release(djp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebBatchProgram()
{
    LOG("Loading {Leb-Batch-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_BATCH];
    djg_program *djp = djgp_create();

    djgp_push_string(djp,
                     "#define BUFFER_BINDING_DRAW_ARRAYS_INDIRECT_COMMAND %i\n",
                     BUFFER_LEB_DRAW_ARRAYS);
    djgp_push_string(djp,
                     "#define BUFFER_BINDING_DISPATCH_INDIRECT_COMMAND %i\n",
                     BUFFER_LEB_DISPATCH);
    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebBatch.glsl");
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    djgp_release(djp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebReductionPrepassProgram()
{
    LOG("Loading {Leb-Reduction-Prepass-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS];
    djg_program *djp = djgp_create();

    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebReductionPrepass.glsl");
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    djgp_release(djp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebReductionProgram()
{
    LOG("Loading {Leb-Reduction-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_REDUCTION];
    djg_program *djp = djgp_create();

    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebReduction.glsl");
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    djgp_release(djp);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebSamplingProgram()
{
    LOG("Loading {Leb-Sampling-Program}");
    GLuint *program = &g_gl.programs[PROGRAM_LEB_SAMPLING];
    djg_program *djp = djgp_create();

    AppendLebHeader(djp);
    djgp_push_file(djp, PATH_TO_SRC_DIRECTORY "./shaders/LebSampling.glsl");
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        djgp_release(djp);

        return false;
    }

    djgp_release(djp);

    g_gl.uniforms[UNIFORM_LEB_SAMPLING_MVP_MATRIX] =
        glGetUniformLocation(*program, "u_ModelViewProjectionMatrix");

    return glGetError() == GL_NO_ERROR;
}

bool LoadPrograms()
{
    bool isLoaded = true;

    if (isLoaded) isLoaded = LoadLebBatchProgram();
    if (isLoaded) isLoaded = LoadLebMergeProgram();
    if (isLoaded) isLoaded = LoadLebRenderProgram();
    if (isLoaded) isLoaded = LoadLebSplitProgram();
    if (isLoaded) isLoaded = LoadLebReductionPrepassProgram();
    if (isLoaded) isLoaded = LoadLebReductionProgram();
    if (isLoaded) isLoaded = LoadLebSamplingProgram();

    return isLoaded;
}

////////////////////////////////////////////////////////////////////////////////
// Buffer Loading
//
////////////////////////////////////////////////////////////////////////////////

bool LoadLebHeapBuffer()
{
    leb_Heap *leb = leb_CreateMinMax(1, g_demo.leb.depth);
    GLuint *buffer = &g_gl.buffers[BUFFER_LEB_HEAP];
    int lebBufferByteSize = leb_HeapByteSize(leb) + 2 * sizeof(int32_t);
    uint32_t *lebBufferData = (uint32_t *)malloc(lebBufferByteSize);

    lebBufferData[0] = leb_MinDepth(leb);
    lebBufferData[1] = leb_MaxDepth(leb);
    memcpy(&lebBufferData[2], leb_GetHeapMemory(leb), leb_HeapByteSize(leb));

    leb_ResetToDepth(leb, 1);
    LOG("Loading {Leb-Heap-Buffer}");
    if (glIsBuffer(*buffer))
        glDeleteBuffers(1, buffer);

    glGenBuffers(1, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    lebBufferByteSize,
                    lebBufferData,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    free(lebBufferData);

    return glGetError() == GL_NO_ERROR;
}

bool LoadLebDrawArraysBuffer()
{
    LOG("Loading {Leb-Draw-Arrays-Buffer}");
    uint32_t drawArraysBuffer[8] = {2, 1, 0, 0, 0, 0, 0, 0};
    GLuint *buffer = &g_gl.buffers[BUFFER_LEB_DRAW_ARRAYS];

    if (glIsBuffer(*buffer))
        glDeleteBuffers(1, buffer);

    glGenBuffers(1, buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, *buffer);
    glBufferStorage(GL_DRAW_INDIRECT_BUFFER,
                    sizeof(drawArraysBuffer),
                    drawArraysBuffer,
                    0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

bool LoadLebDispatchBuffer()
{
    LOG("Loading {Leb-Dispatch-Buffer}");
    uint32_t dispatchBuffer[8] = {1, 1, 1, 0, 0, 0, 0, 0};
    GLuint *buffer = &g_gl.buffers[BUFFER_LEB_DISPATCH];

    if (glIsBuffer(*buffer))
        glDeleteBuffers(1, buffer);

    glGenBuffers(1, buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, *buffer);
    glBufferStorage(GL_DRAW_INDIRECT_BUFFER,
                    sizeof(dispatchBuffer),
                    dispatchBuffer,
                    0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

/*
  Van der Corput Sampling

 */
float VanDerCorputSample(uint32_t x)
{
    x = ( x               << 16) | ( x               >> 16);
    x = ((x & 0x00FF00FF) <<  8) | ((x & 0xFF00FF00) >>  8);
    x = ((x & 0x0F0F0F0F) <<  4) | ((x & 0xF0F0F0F0) >>  4);
    x = ((x & 0x33333333) <<  2) | ((x & 0xCCCCCCCC) >>  2);
    x = ((x & 0x55555555) <<  1) | ((x & 0xAAAAAAAA) >>  1);

    return (float)x / (float)0x100000000LL;
}

bool LoadRandomBuffer()
{
    int sampleCount = g_demo.samples.count;
    size_t bufferByteSize = sizeof(float) * sampleCount;
    float *bufferData = (float *)malloc(bufferByteSize);
    GLuint *buffer = &g_gl.buffers[BUFFER_RANDOM];

    for (uint32_t sampleID = 0; sampleID < sampleCount; ++sampleID) {
        bufferData[sampleID] = VanDerCorputSample(sampleID);
    }

    glGenBuffers(1, buffer);
    glBindBuffer(GL_ARRAY_BUFFER, *buffer);
    glBufferStorage(GL_ARRAY_BUFFER, bufferByteSize, bufferData, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    free(bufferData);

    return glGetError() == GL_NO_ERROR;
}

bool LoadBuffers()
{
    bool isLoaded = true;

    if (isLoaded) isLoaded = LoadLebDrawArraysBuffer();
    if (isLoaded) isLoaded = LoadLebDispatchBuffer();
    if (isLoaded) isLoaded = LoadLebHeapBuffer();
    if (isLoaded) isLoaded = LoadRandomBuffer();

    return isLoaded;
}

////////////////////////////////////////////////////////////////////////////////
// Texture Loading
//
////////////////////////////////////////////////////////////////////////////////

void LoadDensityTexture(const djg_texture *djt)
{
    int width = djt->next->x;
    int height = djt->next->y;
    float *texels = (float *)malloc(width * height * /*RG*/2 * /*float*/4);
    const float *texelsIn = (float *)djt->next->texels;
    GLuint *texture = &g_gl.textures[TEXTURE_DENSITY];
    float nrm = 0.0f;

    for (int j = 0; j < height; ++j)
    for (int i = 0; i < width; ++i) {
            int tmp = i + width * j;
            float r = texelsIn[0 + 3 * tmp];
            float g = texelsIn[1 + 3 * tmp];
            float b = texelsIn[2 + 3 * tmp];
            float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;

            nrm+= l;
            texels[0 + 2 * tmp] = l;
            texels[1 + 2 * tmp] = l * l;
    }

    nrm = width * height / nrm;
    for (int i = 0; i < width * height; ++i) {
        texels[0 + 2 * i]*= nrm;
        texels[1 + 2 * i]*= nrm * nrm;
    }

    glGenTextures(1, texture);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_DENSITY);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexStorage2D(GL_TEXTURE_2D,
                   djgt__mipcnt(width, height, 0),
                   GL_RG32F,
                   width,
                   height);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0, 0, width, height,
                    GL_RG,
                    GL_FLOAT,
                    texels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    free(texels);
}

bool LoadImageTexture()
{
    LOG("Loading {Image-Texture}");
    djg_texture *djt = djgt_create(3); // force RGB
    const char *imagePath = g_demo.image.path;
    GLuint *texture = &g_gl.textures[TEXTURE_IMAGE];

    glActiveTexture(GL_TEXTURE0 + TEXTURE_IMAGE);
    djgt_push_image_hdr(djt, imagePath, true);
    if (!djgt_to_gl(djt, GL_TEXTURE_2D, GL_RGBA16F, true, true, texture)) {
        djgt_release(djt);

        return false;
    }
    glActiveTexture(GL_TEXTURE0);

    LoadDensityTexture(djt);
    djgt_release(djt);

    return true;
}

bool LoadTextures()
{
    bool isLoaded = true;

    if (isLoaded) isLoaded = LoadImageTexture();

    return isLoaded;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Array Loading
//
////////////////////////////////////////////////////////////////////////////////

bool LoadEmptyVertexArray()
{
    LOG("Loading {Empty-Vertex-Array}");
    GLuint *vertexArray = &g_gl.vertexArrays[VERTEXARRAY_EMPTY];

    glGenVertexArrays(1, vertexArray);
    glBindVertexArray(*vertexArray);
    glBindVertexArray(0);

    return glGetError() == GL_NO_ERROR;
}

bool LoadSamplingVertexArray()
{
    LOG("Loading {Sampling-Vertex-Array}");
    GLuint *vertexArray = &g_gl.vertexArrays[VERTEXARRAY_SAMPLING];

    glGenVertexArrays(1, vertexArray);
    glBindVertexArray(*vertexArray);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_RANDOM]);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
    glBindVertexArray(0);

    return glGetError() == GL_NO_ERROR;
}

bool LoadVertexArrays()
{
    bool isLoaded = true;

    if (isLoaded) isLoaded = LoadEmptyVertexArray();
    if (isLoaded) isLoaded = LoadSamplingVertexArray();

    return isLoaded;
}

////////////////////////////////////////////////////////////////////////////////
// Application Loading
//
////////////////////////////////////////////////////////////////////////////////

bool Load(int argc, char **argv)
{
    bool isLoaded = true;

    if (isLoaded) isLoaded = LoadTextures();
    if (isLoaded) isLoaded = LoadBuffers();
    if (isLoaded) isLoaded = LoadVertexArrays();
    if (isLoaded) isLoaded = LoadPrograms();

    return isLoaded;
}

void Release()
{
    glDeleteTextures(TEXTURE_COUNT, g_gl.textures);
    glDeleteTextures(BUFFER_COUNT, g_gl.buffers);
    glDeleteTextures(VERTEXARRAY_COUNT, g_gl.vertexArrays);
    for (int i = 0; i < PROGRAM_COUNT; ++i)
        glDeleteProgram(g_gl.programs[i]);

}

////////////////////////////////////////////////////////////////////////////////
// Updating
//
////////////////////////////////////////////////////////////////////////////////

void ComputeLebReduction()
{
    int it = g_demo.leb.depth;

    glUseProgram(g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS]);
    if (true) {
        int cnt = (1 << it) >> 5;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS],
                                       "u_PassID");

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        it-= 5;
    }

    glUseProgram(g_gl.programs[PROGRAM_LEB_REDUCTION]);
    while (--it >= 0) {
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_LEB_REDUCTION],
                                       "u_PassID");
        int cnt = 1 << it;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void ComputeLebBatch()
{
    glUseProgram(g_gl.programs[PROGRAM_LEB_BATCH]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB_DRAW_ARRAYS,
                     g_gl.buffers[BUFFER_LEB_DRAW_ARRAYS]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB_DISPATCH,
                     g_gl.buffers[BUFFER_LEB_DISPATCH]);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_DRAW_ARRAYS, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_DISPATCH, 0);
}

void ComputeLebUpdate()
{
    static int splitOrMerge = 0;

    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                 g_gl.buffers[BUFFER_LEB_DISPATCH]);
    glUseProgram(g_gl.programs[PROGRAM_LEB_MERGE + splitOrMerge]);
    glDispatchComputeIndirect(0);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);

    splitOrMerge = 1 - splitOrMerge;
}

void UpdateLeb()
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB_HEAP,
                     g_gl.buffers[BUFFER_LEB_HEAP]);
    ComputeLebUpdate();
    ComputeLebReduction();
    ComputeLebBatch();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_HEAP, 0);
}

dja::mat4 GetCameraMatrix()
{
    float zoomFactor = exp2f(-g_demo.camera.zoom);
    float x = g_demo.camera.pos.x;
    float y = g_demo.camera.pos.y;

    return dja::mat4::homogeneous::orthographic(
        x - zoomFactor + 0.50001f, x + zoomFactor + 0.5f,
        y - zoomFactor + 0.5f, y + zoomFactor + 0.5f,
        -1.0f, 1.0f
    );
}

void RenderLeb()
{
    dja::mat4 m = GetCameraMatrix();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB_HEAP,
                     g_gl.buffers[BUFFER_LEB_HEAP]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_LEB_DRAW_ARRAYS]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glUseProgram(g_gl.programs[PROGRAM_LEB_RENDER]);
    glUniformMatrix4fv(g_gl.uniforms[UNIFORM_LEB_RENDER_MVP_MATRIX],
                       1, GL_TRUE, &m[0][0]);
    glDrawArraysIndirect(GL_POINTS, BUFFER_OFFSET(0));
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_HEAP, 0);
    glUseProgram(0);
    glBindVertexArray(0);
}

void RenderSamples()
{
    dja::mat4 m = GetCameraMatrix();

    glPointSize(8.0f);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB_HEAP,
                     g_gl.buffers[BUFFER_LEB_HEAP]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SAMPLING]);
    glUseProgram(g_gl.programs[PROGRAM_LEB_SAMPLING]);
    glUniformMatrix4fv(g_gl.uniforms[UNIFORM_LEB_SAMPLING_MVP_MATRIX],
                       1, GL_TRUE, &m[0][0]);
    glDrawArrays(GL_POINTS, 0, g_demo.samples.active);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BUFFER_LEB_HEAP, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glPointSize(1.0f);
}

void Render()
{
    glClear(GL_COLOR_BUFFER_BIT);

    UpdateLeb();

    glViewport(256, 0, VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    RenderLeb();
    RenderSamples();
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
        const char* eTonemaps[] = {
            "Uncharted2",
            "Filmic",
            "Aces",
            "Reinhard",
            "Raw"
        };
        if (ImGui::Combo("Tonemap", &g_demo.camera.tonemap, &eTonemaps[0], BUFFER_SIZE(eTonemaps)))
            LoadLebRenderProgram();
        if (ImGui::Checkbox("ShowLeb", &g_demo.flags.showLeb))
            LoadLebRenderProgram();
        if (ImGui::SliderFloat("TargetDeviation", &g_demo.leb.targetStdev, 0.0f, 2.0f)) {
            ConfigureLebUpdatePrograms();
        }
        ImGui::SliderInt("SampleCount", &g_demo.samples.active, 0, 256);
        ImGui::Text("Pos : %f %f", g_demo.camera.pos.x, g_demo.camera.pos.y);
        ImGui::Text("Zoom: %f", g_demo.camera.zoom);
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
            LoadPrograms();
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
        float sc = exp2f(-g_demo.camera.zoom);
        float dx = x - x0, dy = y - y0;

        g_demo.camera.pos.x-= dx * sc * 2e-3;
        g_demo.camera.pos.y+= dy * sc * 2e-3;
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        g_demo.camera.zoom+= (x - x0) * 1e-2;

        if (g_demo.camera.zoom < -1.0f)
            g_demo.camera.zoom = -1.0f;
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
    LOG("Loading {Window-Main}");
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH+256, VIEWPORT_WIDTH, "Viewer", NULL, NULL);
    if (window == NULL) {
        LOG("=> Failure <=");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, &KeyboardCallback);
    glfwSetCursorPosCallback(window, &MouseMotionCallback);
    glfwSetMouseButtonCallback(window, &MouseButtonCallback);
    glfwSetScrollCallback(window, &MouseScrollCallback);

    // Load OpenGL functions
    LOG("Loading {OpenGL}");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("gladLoadGLLoader failed");
        return -1;
    }

    SetupDebugOutput();

    LOG("-- Begin -- Demo");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 450");
    if (!Load(argc, argv)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();

        return -1;
    }

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

    LOG("-- End -- Demo");

    return 0;
}

