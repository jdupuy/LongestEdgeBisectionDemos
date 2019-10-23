//////////////////////////////////////////////////////////////////////////////
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
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"

#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#define LEB_IMPLEMENTATION
#include "LongestEdgeBisection.h"

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);

////////////////////////////////////////////////////////////////////////////////
// Tweakable Constants
//
////////////////////////////////////////////////////////////////////////////////
#define VIEWER_DEFAULT_WIDTH  1680
#define VIEWER_DEFAULT_HEIGHT 1050

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

////////////////////////////////////////////////////////////////////////////////
// Global Variables
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Framebuffer Manager
enum { AA_NONE, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_MSAA16 };
struct FramebufferManager {
    int w, h, aa;
    struct { int fixed; } msaa;
    struct { float r, g, b; } clearColor;
} g_framebuffer = {
    VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT, AA_NONE,
    {false},
    {61.0f / 255.0f, 119.0f / 255.0f, 192.0f / 255.0f}
};

// -----------------------------------------------------------------------------
// Camera Manager
#define INIT_POS dja::vec3(-2.5f, -2.0f, 1.25f)
enum {
    TONEMAP_UNCHARTED2,
    TONEMAP_FILMIC,
    TONEMAP_ACES,
    TONEMAP_REINHARD,
    TONEMAP_RAW
};
struct CameraManager {
    float fovy, zNear, zFar; // perspective settings
    int toneMapper;          // tone mapping technique
    dja::vec3 pos;           // 3D position
    dja::mat3 axis;          // 3D frame
    float upAngle, sideAngle; // rotation axis
} g_camera = {
    80.f, 0.01f, 32.f,
    TONEMAP_RAW,
    INIT_POS,
    dja::mat3(1.0f),
    3.5f, 0.4f
};
#undef INIT_POS
void updateCameraMatrix()
{
    float c1 = cos(g_camera.upAngle);
    float s1 = sin(g_camera.upAngle);
    float c2 = cos(g_camera.sideAngle);
    float s2 = sin(g_camera.sideAngle);

    g_camera.axis = dja::mat3(
        c1 * c2, -s1, -c1 * s2,
        c2 * s1,  c1, -s1 * s2,
        s2     , 0.0f, c2
    );
}

// -----------------------------------------------------------------------------
// Terrain Manager
enum { METHOD_TS, METHOD_GS, METHOD_MS };
enum { SHADING_SNOWY, SHADING_DIFFUSE, SHADING_NORMALS, SHADING_COLOR};
struct TerrainManager {
    struct { bool displace, cull, freeze, wire; } flags;
    struct {
        std::string pathToFile;
        float scale;
    } dmap;
    int method;
    int shading;
    int gpuSubd;
    float primitivePixelLengthTarget;
    float minLodStdev;
    int maxDepth;
    float size;
} g_terrain = {
    {true, true, false, false},
    {std::string(PATH_TO_ASSET_DIRECTORY "./Terrain4k.png"), 0.2f},
    METHOD_GS,
    SHADING_DIFFUSE,
    3,
    7.0f,
    0.0010f,
    24,
    8
};

// -----------------------------------------------------------------------------
// Application Manager
struct AppManager {
    struct {
        const char *shader;
        const char *output;
    } dir;
    struct {
        int w, h;
        bool hud;
        float gamma, exposure;
    } viewer;
    struct {
        int on, frame, capture;
    } recorder;
    int frame, frameLimit;
} g_app = {
    /*dir*/     {
                    PATH_TO_SRC_DIRECTORY "./shaders/",
                    PATH_TO_SRC_DIRECTORY "./"
                },
    /*viewer*/  {
                   VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
                   true,
                   2.2f, 0.4f
                },
    /*record*/  {false, 0, 0},
    /*frame*/   0, -1
};

// -----------------------------------------------------------------------------
// OpenGL Manager

enum {
    CLOCK_BATCH,
    CLOCK_RENDER,
    CLOCK_MIPMAP,
    CLOCK_MIPMAP00,
    CLOCK_MIPMAP01,
    CLOCK_MIPMAP02,
    CLOCK_MIPMAP03,
    CLOCK_MIPMAP04,
    CLOCK_MIPMAP05,
    CLOCK_MIPMAP06,
    CLOCK_MIPMAP07,
    CLOCK_MIPMAP08,
    CLOCK_MIPMAP09,
    CLOCK_MIPMAP10,
    CLOCK_MIPMAP11,
    CLOCK_MIPMAP12,
    CLOCK_MIPMAP13,
    CLOCK_MIPMAP14,
    CLOCK_MIPMAP15,
    CLOCK_MIPMAP16,
    CLOCK_MIPMAP17,
    CLOCK_MIPMAP18,
    CLOCK_MIPMAP19,
    CLOCK_MIPMAP20,
    CLOCK_MIPMAP21,
    CLOCK_MIPMAP22,
    CLOCK_MIPMAP23,
    CLOCK_MIPMAP24,
    CLOCK_MIPMAP25,
    CLOCK_MIPMAP26,
    CLOCK_MIPMAP27,
    CLOCK_MIPMAP28,
    CLOCK_MIPMAP29,
    CLOCK_COUNT
};
enum { FRAMEBUFFER_BACK, FRAMEBUFFER_SCENE, FRAMEBUFFER_COUNT };
enum { STREAM_TERRAIN_VARIABLES, STREAM_COUNT };
enum { VERTEXARRAY_EMPTY, VERTEXARRAY_COUNT };
enum { BUFFER_LEB, BUFFER_TERRAIN_DRAW, BUFFER_TERRAIN_DRAW_MS, BUFFER_COUNT };
enum {
    TEXTURE_CBUF,
    TEXTURE_ZBUF,
    TEXTURE_DMAP,
    TEXTURE_SMAP,
    TEXTURE_COUNT
};
enum {
    PROGRAM_VIEWER,
    PROGRAM_SPLIT_AND_RENDER,
    PROGRAM_MERGE_AND_RENDER,
    PROGRAM_TOPVIEW,
    PROGRAM_LEB_REDUCTION,
    PROGRAM_LEB_REDUCTION_PREPASS,
    PROGRAM_BATCH,
    PROGRAM_COUNT
};
enum {
    UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER,
    UNIFORM_VIEWER_GAMMA,

    UNIFORM_TERRAIN_DMAP_SAMPLER,
    UNIFORM_TERRAIN_SMAP_SAMPLER,
    UNIFORM_TERRAIN_DMAP_FACTOR,
    UNIFORM_TERRAIN_TARGET_EDGE_LENGTH,
    UNIFORM_TERRAIN_LOD_FACTOR,
    UNIFORM_TERRAIN_MIN_LOD_VARIANCE,

    UNIFORM_TOPVIEW_DMAP_SAMPLER,
    UNIFORM_TOPVIEW_DMAP_FACTOR,

    UNIFORM_COUNT
};
struct OpenGLManager {
    GLuint programs[PROGRAM_COUNT];
    GLuint framebuffers[FRAMEBUFFER_COUNT];
    GLuint textures[TEXTURE_COUNT];
    GLuint vertexArrays[VERTEXARRAY_COUNT];
    GLuint buffers[BUFFER_COUNT];
    GLint uniforms[UNIFORM_COUNT];
    djg_buffer *streams[STREAM_COUNT];
    djg_clock *clocks[CLOCK_COUNT];
} g_gl = {
    .programs       = {0},
    .framebuffers   = {0},
    .textures       = {0},
    .vertexArrays   = {0},
    .buffers        = {0},
    .uniforms       = {0},
    .streams        = {NULL},
    .clocks         = {NULL}
};

////////////////////////////////////////////////////////////////////////////////
// Utility functions
//
////////////////////////////////////////////////////////////////////////////////

#ifndef M_PI
#define M_PI 3.141592654
#endif
#define BUFFER_SIZE(x)    ((int)(sizeof(x)/sizeof(x[0])))
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

float radians(float degrees)
{
    return degrees * M_PI / 180.f;
}

char *strcat2(char *dst, const char *src1, const char *src2)
{
    strcpy(dst, src1);

    return strcat(dst, src2);
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

////////////////////////////////////////////////////////////////////////////////
// Program Configuration
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// set viewer program uniforms
void configureViewerProgram()
{
    glProgramUniform1i(g_gl.programs[PROGRAM_VIEWER],
        g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER],
        TEXTURE_CBUF);
    glProgramUniform1f(g_gl.programs[PROGRAM_VIEWER],
        g_gl.uniforms[UNIFORM_VIEWER_GAMMA],
        g_app.viewer.gamma);
}

// -----------------------------------------------------------------------------
// set Terrain program uniforms
void configureTerrainProgram(GLuint glp)
{
    float tmp = 2.0f * tan(radians(g_camera.fovy) / 2.0f)
        / g_framebuffer.h * (1 << g_terrain.gpuSubd)
        * g_terrain.primitivePixelLengthTarget;
    float lodFactor = -2.0f * std::log2(tmp) + 2.0f;

    glProgramUniform1f(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_DMAP_FACTOR],
        g_terrain.dmap.scale);
    glProgramUniform1f(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_LOD_FACTOR],
        lodFactor);
    glProgramUniform1i(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_DMAP_SAMPLER],
        TEXTURE_DMAP);
    glProgramUniform1i(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_SMAP_SAMPLER],
        TEXTURE_SMAP);
    glProgramUniform1f(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_TARGET_EDGE_LENGTH],
        g_terrain.primitivePixelLengthTarget);
    glProgramUniform1f(glp,
        g_gl.uniforms[UNIFORM_TERRAIN_MIN_LOD_VARIANCE],
        g_terrain.minLodStdev * g_terrain.minLodStdev / (g_terrain.dmap.scale * g_terrain.dmap.scale));
}

void configureTerrainPrograms()
{
    configureTerrainProgram(g_gl.programs[PROGRAM_SPLIT_AND_RENDER]);
    configureTerrainProgram(g_gl.programs[PROGRAM_MERGE_AND_RENDER]);
}

// -----------------------------------------------------------------------------
// set Terrain program uniforms
void configureTopViewProgram()
{
    glProgramUniform1f(g_gl.programs[PROGRAM_TOPVIEW],
        g_gl.uniforms[UNIFORM_TOPVIEW_DMAP_FACTOR],
        g_terrain.dmap.scale);
    glProgramUniform1i(g_gl.programs[PROGRAM_TOPVIEW],
        g_gl.uniforms[UNIFORM_TOPVIEW_DMAP_SAMPLER],
        TEXTURE_DMAP);
}

///////////////////////////////////////////////////////////////////////////////
// Program Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Viewer Program
 *
 * This program is responsible for blitting the scene framebuffer to
 * the back framebuffer, while applying gamma correction and tone mapping to
 * the rendering.
 */
bool loadViewerProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_VIEWER];
    char buf[1024];

    LOG("Loading {Viewer-Program}\n");
    if (g_framebuffer.aa >= AA_MSAA2 && g_framebuffer.aa <= AA_MSAA16)
        djgp_push_string(djp, "#define MSAA_FACTOR %i\n", 1 << g_framebuffer.aa);
    switch (g_camera.toneMapper) {
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
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ToneMapping.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "Viewer.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_FramebufferSampler");
    g_gl.uniforms[UNIFORM_VIEWER_GAMMA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Gamma");

    configureViewerProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Terrain Rendering Program
 *
 * This program is responsible for updating and rendering the terrain.
 */
bool loadTerrainRenderingProgram(GLuint *glp, const char *flag)
{
    djg_program *djp = djgp_create();
    char buf[1024];

    LOG("Loading {Terrain-Rendering-Program}\n");
    if (!g_terrain.flags.freeze)
        djgp_push_string(djp, flag);
    if (g_terrain.method == METHOD_MS) {
        djgp_push_string(djp, "#ifndef FRAGMENT_SHADER\n#extension GL_NV_mesh_shader : require\n#endif\n");
        djgp_push_string(djp, "#extension GL_NV_shader_thread_group : require\n");
        djgp_push_string(djp, "#extension GL_NV_shader_thread_shuffle : require\n");
        djgp_push_string(djp, "#extension GL_NV_gpu_shader5 : require\n");
    }
    djgp_push_string(djp, "#define BUFFER_BINDING_TERRAIN_VARIABLES %i\n", STREAM_TERRAIN_VARIABLES);
    djgp_push_string(djp, "#define TERRAIN_PATCH_SUBD_LEVEL %i\n", g_terrain.gpuSubd);
    djgp_push_string(djp, "#define TERRAIN_PATCH_TESS_FACTOR %i\n", 1 << g_terrain.gpuSubd);
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB);
    djgp_push_string(djp, "#define LEB_MAX_DEPTH %i\n", g_terrain.maxDepth);
    if (g_terrain.shading == SHADING_DIFFUSE)
        djgp_push_string(djp, "#define SHADING_DIFFUSE 1\n");
    else if (g_terrain.shading == SHADING_NORMALS)
        djgp_push_string(djp, "#define SHADING_NORMALS 1\n");
    else if (g_terrain.shading == SHADING_SNOWY)
        djgp_push_string(djp, "#define SHADING_SNOWY 1\n");
    else if (g_terrain.shading == SHADING_COLOR)
        djgp_push_string(djp, "#define SHADING_COLOR 1\n");
    if (g_terrain.flags.displace)
        djgp_push_string(djp, "#define FLAG_DISPLACE 1\n");
    if (g_terrain.flags.cull)
        djgp_push_string(djp, "#define FLAG_CULL 1\n");
    if (g_terrain.flags.wire)
        djgp_push_string(djp, "#define FLAG_WIRE 1\n");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "FrustumCulling.glsl"));
    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainRenderCommon.glsl"));
    if (g_terrain.method == METHOD_TS) {
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainRenderTS.glsl"));
    } else if (g_terrain.method == METHOD_GS) {
        int subdLevel = g_terrain.gpuSubd;
        int vertexCnt = subdLevel == 0 ? 3 : 4 << (2 * subdLevel - 1);

        djgp_push_string(djp, "#define MAX_VERTICES %i\n", vertexCnt);
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainRenderGS.glsl"));
    }
    else if (g_terrain.method == METHOD_MS) {
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainRenderMS.glsl"));
    }

    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_TERRAIN_DMAP_FACTOR] =
        glGetUniformLocation(*glp, "u_DmapFactor");
    g_gl.uniforms[UNIFORM_TERRAIN_LOD_FACTOR] =
        glGetUniformLocation(*glp, "u_LodFactor");
    g_gl.uniforms[UNIFORM_TERRAIN_DMAP_SAMPLER] =
        glGetUniformLocation(*glp, "u_DmapSampler");
    g_gl.uniforms[UNIFORM_TERRAIN_SMAP_SAMPLER] =
        glGetUniformLocation(*glp, "u_SmapSampler");
    g_gl.uniforms[UNIFORM_TERRAIN_TARGET_EDGE_LENGTH] =
        glGetUniformLocation(*glp, "u_TargetEdgeLength");
    g_gl.uniforms[UNIFORM_TERRAIN_MIN_LOD_VARIANCE] =
        glGetUniformLocation(*glp, "u_MinLodVariance");

    configureTerrainProgram(*glp);

    return (glGetError() == GL_NO_ERROR);
}

bool loadTerrainRenderingPrograms()
{
    bool v = true;

    if (v) v = v && loadTerrainRenderingProgram(&g_gl.programs[PROGRAM_SPLIT_AND_RENDER], "#define FLAG_SPLIT 1\n");
    if (v) v = v && loadTerrainRenderingProgram(&g_gl.programs[PROGRAM_MERGE_AND_RENDER], "#define FLAG_MERGE 1\n");

    return v;
}

// -----------------------------------------------------------------------------
/**
 * Load the Reduction Program
 *
 * This program is responsible for precomputing a reduction for the
 * subdivision tree. This allows to locate the i-th bit in a bitfield of
 * size N in log(N) operations.
 */
bool loadLebReductionProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_LEB_REDUCTION];
    char buf[1024];

    LOG("Loading {Reduction-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB);
    djgp_push_string(djp, "#define LEB_MAX_DEPTH %i\n", g_terrain.maxDepth);
    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "LongestEdgeBisectionSumReduction.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    return (glGetError() == GL_NO_ERROR);
}

bool loadLebReductionPrepassProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS];
    char buf[1024];

    LOG("Loading {Reduction-Prepass-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB);
    djgp_push_string(djp, "#define LEB_MAX_DEPTH %i\n", g_terrain.maxDepth);
    djgp_push_string(djp, "#define LEB_REDUCTION_PREPASS\n");
    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "LongestEdgeBisectionSumReduction.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Batch Program
 *
 * This program is responsible for preparing an indirect draw call
 */
bool loadBatchProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_BATCH];
    char buf[1024];

    LOG("Loading {Batch-Program}\n");
    if (g_terrain.method == METHOD_MS) {
        djgp_push_string(djp, "#define FLAG_MS 1\n");
    }
    djgp_push_string(djp, "#define LEB_MAX_DEPTH %i\n", g_terrain.maxDepth);
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB);
    djgp_push_string(djp, "#define BUFFER_BINDING_DRAW_ARRAYS_INDIRECT_COMMAND %i\n", BUFFER_TERRAIN_DRAW);
    djgp_push_string(djp, "#define BUFFER_BINDING_DRAW_MESH_TASKS_INDIRECT_COMMAND %i\n", BUFFER_TERRAIN_DRAW_MS);
    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "LongestEdgeBisectionBatcher.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Top View Program
 *
 * This program is responsible for rendering the terrain in a top view fashion
 */
bool loadTopViewProgram()
{
    djg_program *djp = djgp_create();
    GLuint *glp = &g_gl.programs[PROGRAM_TOPVIEW];
    char buf[1024];

    LOG("Loading {Top-View-Program}\n");
    if (g_terrain.flags.displace)
        djgp_push_string(djp, "#define FLAG_DISPLACE 1\n");
    djgp_push_string(djp, "#define TERRAIN_PATCH_SUBD_LEVEL %i\n", g_terrain.gpuSubd);
    djgp_push_string(djp, "#define TERRAIN_PATCH_TESS_FACTOR %i\n", 1 << g_terrain.gpuSubd);
    djgp_push_string(djp, "#define BUFFER_BINDING_TERRAIN_VARIABLES %i\n", STREAM_TERRAIN_VARIABLES);
    djgp_push_string(djp, "#define BUFFER_BINDING_LEB %i\n", BUFFER_LEB);
    djgp_push_string(djp, "#define LEB_MAX_DEPTH %i\n", g_terrain.maxDepth);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "FrustumCulling.glsl"));
    djgp_push_file(djp, PATH_TO_LEB_GLSL_LIBRARY "LongestEdgeBisection.glsl");
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainRenderCommon.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "TerrainTopView.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, glp)) {
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_TOPVIEW_DMAP_FACTOR] =
        glGetUniformLocation(*glp, "u_DmapFactor");
    g_gl.uniforms[UNIFORM_TOPVIEW_DMAP_SAMPLER] =
        glGetUniformLocation(*glp, "u_DmapSampler");

    configureTopViewProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Programs
 *
 */
bool loadPrograms()
{
    bool v = true;

    if (v) v &= loadViewerProgram();
    if (v) v &= loadTerrainRenderingPrograms();
    if (v) v &= loadLebReductionProgram();
    if (v) v &= loadLebReductionPrepassProgram();
    if (v) v &= loadBatchProgram();
    if (v) v &= loadTopViewProgram();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// Texture Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer Textures
 *
 * Depending on the scene framebuffer AA mode, this function load 2 or
 * 3 textures. In FSAA mode, two RGBA16F and one DEPTH24_STENCIL8 textures
 * are created. In other modes, one RGBA16F and one DEPTH24_STENCIL8 textures
 * are created.
 */
bool loadSceneFramebufferTexture()
{
    if (glIsTexture(g_gl.textures[TEXTURE_CBUF]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_CBUF]);
    if (glIsTexture(g_gl.textures[TEXTURE_ZBUF]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_ZBUF]);
    glGenTextures(1, &g_gl.textures[TEXTURE_ZBUF]);
    glGenTextures(1, &g_gl.textures[TEXTURE_CBUF]);

    switch (g_framebuffer.aa) {
    case AA_NONE:
        LOG("Loading {Z-Buffer-Framebuffer-Texture}\n");
        glActiveTexture(GL_TEXTURE0 + TEXTURE_ZBUF);
        glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_ZBUF]);
        glTexStorage2D(GL_TEXTURE_2D,
            1,
            GL_DEPTH24_STENCIL8,
            g_framebuffer.w,
            g_framebuffer.h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        LOG("Loading {Color-Buffer-Framebuffer-Texture}\n");
        glActiveTexture(GL_TEXTURE0 + TEXTURE_CBUF);
        glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_CBUF]);
        glTexStorage2D(GL_TEXTURE_2D,
            1,
            GL_RGBA32F,
            g_framebuffer.w,
            g_framebuffer.h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    case AA_MSAA2:
    case AA_MSAA4:
    case AA_MSAA8:
    case AA_MSAA16: {
        int samples = 1 << g_framebuffer.aa;

        int maxSamples;
        int maxSamplesDepth;
        //glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &maxSamples); //Wrong enum !
        glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxSamples);
        glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxSamplesDepth);
        maxSamples = maxSamplesDepth < maxSamples ? maxSamplesDepth : maxSamples;

        if (samples > maxSamples) {
            LOG("note: MSAA is %ix\n", maxSamples);
            samples = maxSamples;
        }
        LOG("Loading {Scene-MSAA-Z-Framebuffer-Texture}\n");
        glActiveTexture(GL_TEXTURE0 + TEXTURE_ZBUF);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_gl.textures[TEXTURE_ZBUF]);
        glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                  samples,
                                  GL_DEPTH24_STENCIL8,
                                  g_framebuffer.w,
                                  g_framebuffer.h,
                                  g_framebuffer.msaa.fixed);

        LOG("Loading {Scene-MSAA-RGBA-Framebuffer-Texture}\n");
        glActiveTexture(GL_TEXTURE0 + TEXTURE_CBUF);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_gl.textures[TEXTURE_CBUF]);
        glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                  samples,
                                  GL_RGBA32F,
                                  g_framebuffer.w,
                                  g_framebuffer.h,
                                  g_framebuffer.msaa.fixed);
    } break;
    }
    glActiveTexture(GL_TEXTURE0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Normal Texture Map
 *
 * This loads an RG32F texture used as a slope map
 */
void loadNmapTexture(const djg_texture *dmap)
{
    int w = dmap->next->x;
    int h = dmap->next->y;
    const uint16_t *texels = (const uint16_t *)dmap->next->texels;
    int mipcnt = djgt__mipcnt(w, h, 1);

    std::vector<float> smap(w * h * 2);

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int i1 = std::max(0, i - 1);
            int i2 = std::min(w - 1, i + 1);
            int j1 = std::max(0, j - 1);
            int j2 = std::min(h - 1, j + 1);
            uint16_t px_l = texels[i1 + w * j]; // in [0,2^16-1]
            uint16_t px_r = texels[i2 + w * j]; // in [0,2^16-1]
            uint16_t px_b = texels[i + w * j1]; // in [0,2^16-1]
            uint16_t px_t = texels[i + w * j2]; // in [0,2^16-1]
            float z_l = (float)px_l / 65535.0f; // in [0, 1]
            float z_r = (float)px_r / 65535.0f; // in [0, 1]
            float z_b = (float)px_b / 65535.0f; // in [0, 1]
            float z_t = (float)px_t / 65535.0f; // in [0, 1]
            float slope_x = (float)w * 0.5f * (z_r - z_l);
            float slope_y = (float)h * 0.5f * (z_t - z_b);

            smap[    2 * (i + w * j)] = slope_x;
            smap[1 + 2 * (i + w * j)] = slope_y;
        }

    if (glIsTexture(g_gl.textures[TEXTURE_SMAP]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_SMAP]);

    glGenTextures(1, &g_gl.textures[TEXTURE_SMAP]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_SMAP);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_SMAP]);
    glTexStorage2D(GL_TEXTURE_2D, mipcnt, GL_RG32F, w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG, GL_FLOAT, &smap[0]);

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
}

// -----------------------------------------------------------------------------
/**
 * Load the Displacement Texture
 *
 * This loads an R16 texture used as a displacement map
 */
bool loadDmapTexture()
{
    if (!g_terrain.dmap.pathToFile.empty()) {
        djg_texture *djgt = djgt_create(1);

        LOG("Loading {Dmap-Texture}\n");
        djgt_push_image_u16(djgt, g_terrain.dmap.pathToFile.c_str(), 1);

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

        // load nmap from dmap
        loadNmapTexture(djgt);

        glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP);
        if (glIsTexture(g_gl.textures[TEXTURE_DMAP]))
            glDeleteTextures(1, &g_gl.textures[TEXTURE_DMAP]);

        glGenTextures(1, &g_gl.textures[TEXTURE_DMAP]);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP);
        glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_DMAP]);
        glTexStorage2D(GL_TEXTURE_2D, mipcnt, GL_RG16, w, h);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG, GL_UNSIGNED_SHORT, &dmap[0]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE0);
        djgt_release(djgt);
    }

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Textures
 */
bool loadTextures()
{
    bool v = true;

    if (v) v &= loadSceneFramebufferTexture();
    if (v) v &= loadDmapTexture();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// Buffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load Terrain Variables UBO
 *
 * This procedure updates the transformation matrices; it is updated each frame.
 */
bool loadTerrainVariables()
{
    static bool first = true;
    struct PerFrameVariables {
        dja::mat4 modelViewMatrix,
                  modelViewProjectionMatrix;
        dja::vec4 frustumPlanes[6];
        dja::vec4 align[2];
    } variables;

    if (first) {
        g_gl.streams[STREAM_TERRAIN_VARIABLES] = djgb_create(sizeof(variables));
        first = false;
    }

    // extract view and projection matrices
    dja::mat4 projection = dja::mat4::homogeneous::perspective(
        radians(g_camera.fovy),
        (float)g_framebuffer.w / (float)g_framebuffer.h,
        g_camera.zNear,
        g_camera.zFar
    );
    dja::mat4 viewInv = dja::mat4::homogeneous::translation(g_camera.pos)
        * dja::mat4::homogeneous::from_mat3(g_camera.axis);
    dja::mat4 view = dja::inverse(viewInv);
    dja::mat4 model = dja::mat4::homogeneous::scale(g_terrain.size)
                    * dja::mat4::homogeneous::translation(dja::vec3(-0.5f, -0.5f, 0));

    // set transformations (column-major)
    variables.modelViewMatrix = dja::transpose(view * model);
    variables.modelViewProjectionMatrix = dja::transpose(projection * view * model);

    // extract frustum planes
    dja::mat4 mvp = variables.modelViewProjectionMatrix;
    for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 2; ++j) {
        variables.frustumPlanes[i*2+j].x = mvp[0][3] + (j == 0 ? mvp[0][i] : -mvp[0][i]);
        variables.frustumPlanes[i*2+j].y = mvp[1][3] + (j == 0 ? mvp[1][i] : -mvp[1][i]);
        variables.frustumPlanes[i*2+j].z = mvp[2][3] + (j == 0 ? mvp[2][i] : -mvp[2][i]);
        variables.frustumPlanes[i*2+j].w = mvp[3][3] + (j == 0 ? mvp[3][i] : -mvp[3][i]);
        dja::vec4 tmp = variables.frustumPlanes[i*2+j];
        variables.frustumPlanes[i*2+j]*= dja::norm(dja::vec3(tmp.x, tmp.y, tmp.z));
    }

    // upload to GPU
    djgb_to_gl(g_gl.streams[STREAM_TERRAIN_VARIABLES], (const void *)&variables, NULL);
    djgb_glbindrange(g_gl.streams[STREAM_TERRAIN_VARIABLES],
                     GL_UNIFORM_BUFFER,
                     STREAM_TERRAIN_VARIABLES);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Subdivision Buffer
 *
 * This procedure initializes the subdivision buffer.
 */
bool loadSubdBuffer()
{
    leb_Memory *leb = leb_Create(g_terrain.maxDepth);

    leb_ResetToDepth(leb, 1);

    LOG("Loading {Subd-Buffer}\n");
    if (glIsBuffer(g_gl.buffers[BUFFER_LEB]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_LEB]);
    glGenBuffers(1, &g_gl.buffers[BUFFER_LEB]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_gl.buffers[BUFFER_LEB]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 leb__BufferByteSize(g_terrain.maxDepth),
                 leb->buffer,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB,
                     g_gl.buffers[BUFFER_LEB]);

    leb_Release(leb);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the indirect command buffer
 *
 * This procedure initializes the subdivision buffer.
 */
bool loadRenderCmdBuffer()
{
    uint drawArraysCmd[8] = {2, 1, 0, 0, 0, 0, 0, 0};
    uint drawMeshTasksCmd[8] = {1, 0, 0, 0, 0, 0, 0, 0};

    if (glIsBuffer(g_gl.buffers[BUFFER_TERRAIN_DRAW]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_TERRAIN_DRAW]);

    if (glIsBuffer(g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]);

    glGenBuffers(1, &g_gl.buffers[BUFFER_TERRAIN_DRAW]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW]);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(drawArraysCmd), drawArraysCmd, GL_STATIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    glGenBuffers(1, &g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(drawMeshTasksCmd), drawMeshTasksCmd, GL_STATIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Buffers
 *
 */
bool loadBuffers()
{
    bool v = true;

    if (v) v &= loadTerrainVariables();
    if (v) v &= loadSubdBuffer();
    if (v) v &= loadRenderCmdBuffer();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Array Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load an Empty Vertex Array
 *
 * This will be used to draw procedural geometry, e.g., a fullscreen quad.
 */
bool loadEmptyVertexArray()
{
    LOG("Loading {Empty-VertexArray}\n");
    if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]))
        glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);

    glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindVertexArray(0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Vertex Arrays
 *
 */
bool loadVertexArrays()
{
    bool v = true;

    if (v) v &= loadEmptyVertexArray();

    return v;
}

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer
 *
 * This framebuffer is used to draw the 3D scene.
 * A single framebuffer is created, holding a color and Z buffer.
 * The scene writes directly to it.
 */
bool loadSceneFramebuffer()
{
    LOG("Loading {Scene-Framebuffer}\n");
    if (glIsFramebuffer(g_gl.framebuffers[FRAMEBUFFER_SCENE]))
        glDeleteFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);

    glGenFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);

    if (g_framebuffer.aa >= AA_MSAA2 && g_framebuffer.aa <= AA_MSAA16) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D_MULTISAMPLE,
            g_gl.textures[TEXTURE_CBUF],
            0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_TEXTURE_2D_MULTISAMPLE,
            g_gl.textures[TEXTURE_ZBUF],
            0);
    }
    else {
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            g_gl.textures[TEXTURE_CBUF],
            0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_TEXTURE_2D,
            g_gl.textures[TEXTURE_ZBUF],
            0);
    }

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (GL_FRAMEBUFFER_COMPLETE != glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
        LOG("=> Failure <=\n");

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Framebuffers
 *
 */
bool loadFramebuffers()
{
    bool v = true;

    if (v) v &= loadSceneFramebuffer();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// OpenGL Resource Loading
//
////////////////////////////////////////////////////////////////////////////////

void init()
{
    bool v = true;
    int i;

    for (i = 0; i < CLOCK_COUNT; ++i) {
        if (g_gl.clocks[i])
            djgc_release(g_gl.clocks[i]);
        g_gl.clocks[i] = djgc_create();
    }

    if (v) v &= loadTextures();
    if (v) v &= loadBuffers();
    if (v) v &= loadFramebuffers();
    if (v) v &= loadVertexArrays();
    if (v) v &= loadPrograms();

    updateCameraMatrix();

    if (!v) throw std::exception();
}

void release()
{
    int i;

    for (i = 0; i < CLOCK_COUNT; ++i)
        if (g_gl.clocks[i])
            djgc_release(g_gl.clocks[i]);
    for (i = 0; i < STREAM_COUNT; ++i)
        if (g_gl.streams[i])
            djgb_release(g_gl.streams[i]);
    for (i = 0; i < PROGRAM_COUNT; ++i)
        if (glIsProgram(g_gl.programs[i]))
            glDeleteProgram(g_gl.programs[i]);
    for (i = 0; i < TEXTURE_COUNT; ++i)
        if (glIsTexture(g_gl.textures[i]))
            glDeleteTextures(1, &g_gl.textures[i]);
    for (i = 0; i < BUFFER_COUNT; ++i)
        if (glIsBuffer(g_gl.buffers[i]))
            glDeleteBuffers(1, &g_gl.buffers[i]);
    for (i = 0; i < FRAMEBUFFER_COUNT; ++i)
        if (glIsFramebuffer(g_gl.framebuffers[i]))
            glDeleteFramebuffers(1, &g_gl.framebuffers[i]);
    for (i = 0; i < VERTEXARRAY_COUNT; ++i)
        if (glIsVertexArray(g_gl.vertexArrays[i]))
            glDeleteVertexArrays(1, &g_gl.vertexArrays[i]);
}

////////////////////////////////////////////////////////////////////////////////
// OpenGL Rendering
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
void renderTerrain()
{
    static int pingPong = 0;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LEB,
                     g_gl.buffers[BUFFER_LEB]);

    // configure GL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // render top view
    glViewport(10, 10, 350, 350);
    glUseProgram(g_gl.programs[PROGRAM_TOPVIEW]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW]);
    glPatchParameteri(GL_PATCH_VERTICES, 1);
    glDrawArraysIndirect(GL_PATCHES, 0);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
    glViewport(0, 0, g_framebuffer.w, g_framebuffer.h);

    // render
    glEnable(GL_CULL_FACE);
    if (g_terrain.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(1.0f);
    djgc_start(g_gl.clocks[CLOCK_RENDER]);
    glUseProgram(g_gl.programs[PROGRAM_SPLIT_AND_RENDER + pingPong]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    if (g_terrain.method == METHOD_TS) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW]);
        glPatchParameteri(GL_PATCH_VERTICES, 1);
        glDrawArraysIndirect(GL_PATCHES, 0);
    } else if (g_terrain.method == METHOD_GS) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW]);
        glDrawArraysIndirect(GL_POINTS, 0);
    } else if (g_terrain.method == METHOD_MS) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]);
        glDrawMeshTasksIndirectNV(0);
    }
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
    glLineWidth(1.0f);
    djgc_stop(g_gl.clocks[CLOCK_RENDER]);

    // reset GL state
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    if (g_terrain.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // mipmap
    djgc_start(g_gl.clocks[CLOCK_MIPMAP]);
    int it = g_terrain.maxDepth;

    glUseProgram(g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS]);
    if (true) {
        int cnt = (1 << it) >> 5;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_LEB_REDUCTION_PREPASS],
                                       "u_PassID");

        djgc_start(g_gl.clocks[CLOCK_MIPMAP00 + it - 1]);
        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        djgc_stop(g_gl.clocks[CLOCK_MIPMAP00 + g_terrain.maxDepth - 1]);

        it-= 5;
    }

    glUseProgram(g_gl.programs[PROGRAM_LEB_REDUCTION]);
    while (--it >= 0) {
        int loc = glGetUniformLocation(g_gl.programs[PROGRAM_LEB_REDUCTION], "u_PassID");
        int cnt = 1 << it;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

        djgc_start(g_gl.clocks[CLOCK_MIPMAP00 + it]);
        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        djgc_stop(g_gl.clocks[CLOCK_MIPMAP00 + it]);
    }

    djgc_stop(g_gl.clocks[CLOCK_MIPMAP]);

    // batch
    djgc_start(g_gl.clocks[CLOCK_BATCH]);
    glUseProgram(g_gl.programs[PROGRAM_BATCH]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_TERRAIN_DRAW,
                     g_gl.buffers[BUFFER_TERRAIN_DRAW]);
    if (g_terrain.method == METHOD_MS) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_TERRAIN_DRAW_MS,
                         g_gl.buffers[BUFFER_TERRAIN_DRAW_MS]);
    }
    glDispatchCompute(1, 1, 1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_TERRAIN_DRAW,
                     0);
    if (g_terrain.method == METHOD_MS) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_TERRAIN_DRAW_MS,
                         0);
    }
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    djgc_stop(g_gl.clocks[CLOCK_BATCH]);

    pingPong = 1 - pingPong;
}

// -----------------------------------------------------------------------------
/**
 * Render Scene
 *
 * This procedure renders the scene to the back buffer.
 */
void renderScene()
{
    // render
    loadTerrainVariables();
    renderTerrain();
}

// -----------------------------------------------------------------------------
void renderViewer()
{
    // render framebuffer
    glUseProgram(g_gl.programs[PROGRAM_VIEWER]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // draw HUD
    if (g_app.viewer.hud) {
        // ImGui
        glUseProgram(0);
        // Viewer Widgets
        //ImGui_ImplGlfw_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Camera Widget
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 150), ImGuiCond_FirstUseEver);
        ImGui::Begin("Camera Settings");
        {
            const char* eToneMappings[] = {
                "Uncharted2",
                "Filmic",
                "Aces",
                "Reinhard",
                "Raw"
            };
            const char* eAA[] = {
                "None",
                "MSAAx2",
                "MSAAx4",
                "MSAAx8",
                "MSAAx16"
            };
            if (ImGui::Combo("Sensor", &g_camera.toneMapper, &eToneMappings[0], BUFFER_SIZE(eToneMappings)))
                loadViewerProgram();
            if (ImGui::Combo("AA", &g_framebuffer.aa, &eAA[0], BUFFER_SIZE(eAA))) {
                loadSceneFramebufferTexture();
                loadSceneFramebuffer();
                loadViewerProgram();
            }
            if (ImGui::SliderFloat("FOVY", &g_camera.fovy, 1.0f, 179.0f)) {
                configureTerrainPrograms();
                configureTopViewProgram();
            }
            if (ImGui::SliderFloat("zNear", &g_camera.zNear, 0.01f, 1.f)) {
                if (g_camera.zNear >= g_camera.zFar)
                    g_camera.zNear = g_camera.zFar - 0.01f;
            }
            if (ImGui::SliderFloat("zFar", &g_camera.zFar, 16.f, 4096.f)) {
                if (g_camera.zFar <= g_camera.zNear)
                    g_camera.zFar = g_camera.zNear + 0.01f;
            }

        }
        ImGui::End();

        // Performance Widget
        ImGui::SetNextWindowPos(ImVec2(g_framebuffer.w - 310, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 460), ImGuiCond_FirstUseEver);
        ImGui::Begin("Performance");
        {
            double cpuDt, gpuDt;
            uint32_t bufSize = leb__BufferByteSize(g_terrain.maxDepth);

            if (bufSize < (1 << 10)) {
                ImGui::Text("LEB Buffer Size: %i Bytes", bufSize);
            } else if (bufSize < (1 << 20)) {
                ImGui::Text("LEB Buffer Size: %i KBytes", bufSize >> 10);
            } else {
                ImGui::Text("LEB Buffer Size: %i MBytes", bufSize >> 20);
            }

            djgc_ticks(g_gl.clocks[CLOCK_RENDER], &cpuDt, &gpuDt);
            ImGui::Text("Render    -- CPU: %.3f%s",
                cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                cpuDt < 1. ? "ms" : " s");
            ImGui::SameLine();
            ImGui::Text("GPU: %.3f%s",
                gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                gpuDt < 1. ? "ms" : " s");
            djgc_ticks(g_gl.clocks[CLOCK_MIPMAP], &cpuDt, &gpuDt);
            ImGui::Text("Reduction -- CPU: %.3f%s",
                cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                cpuDt < 1. ? "ms" : " s");
            ImGui::SameLine();
            ImGui::Text("GPU: %.3f%s",
                gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                gpuDt < 1. ? "ms" : " s");
            djgc_ticks(g_gl.clocks[CLOCK_BATCH], &cpuDt, &gpuDt);
            ImGui::Text("Batcher   -- CPU: %.3f%s",
                cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                cpuDt < 1. ? "ms" : " s");
            ImGui::SameLine();
            ImGui::Text("GPU: %.3f%s",
                gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                gpuDt < 1. ? "ms" : " s");
            for (int i = 0; i < g_terrain.maxDepth; ++i) {
                if (i >= g_terrain.maxDepth - 5 && i < g_terrain.maxDepth - 1)
                    continue;

                djgc_ticks(g_gl.clocks[CLOCK_MIPMAP00 + i], &cpuDt, &gpuDt);
                ImGui::Text("Reduction%02i -- CPU: %.3f%s",
                    i,
                    cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                    cpuDt < 1. ? "ms" : " s");
                ImGui::SameLine();
                ImGui::Text("GPU: %.3f%s",
                    gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                    gpuDt < 1. ? "ms" : " s");
            }
        }
        ImGui::End();

        // Terrain Parameters
        ImGui::SetNextWindowPos(ImVec2(270, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 210), ImGuiCond_FirstUseEver);
        ImGui::Begin("Terrain Settings");
        {
            const char* eShadings[] = {
                "Snowy",
                "Diffuse",
                "Normals",
                "Plain Color"
            };
            std::vector<const char *> ePipelines = {
                "Tessellation Shader",
                "Geometry Shader"
            };
            if (GLAD_GL_NV_mesh_shader)
                ePipelines.push_back("Mesh Shader");

            if (ImGui::Combo("Shading", &g_terrain.shading, &eShadings[0], BUFFER_SIZE(eShadings)))
                loadTerrainRenderingPrograms();
            if (ImGui::Combo("GPU Pipeline", &g_terrain.method, &ePipelines[0], ePipelines.size())) {
                loadTerrainRenderingPrograms();
                loadBatchProgram();
            } if (ImGui::Checkbox("cull", &g_terrain.flags.cull))
                loadPrograms();
            ImGui::SameLine();
            ImGui::Checkbox("wire", &g_terrain.flags.wire);
            ImGui::SameLine();
            if (ImGui::Checkbox("freeze", &g_terrain.flags.freeze)) {
                loadTerrainRenderingPrograms();
            }
            if (!g_terrain.dmap.pathToFile.empty()) {
                ImGui::SameLine();
                if (ImGui::Checkbox("displace", &g_terrain.flags.displace)) {
                    loadTerrainRenderingPrograms();
                    loadTopViewProgram();
                }
            }
            if (ImGui::SliderInt("PatchSubdLevel", &g_terrain.gpuSubd, 0, 5)) {
                loadPrograms();
            }
            if (ImGui::SliderFloat("PixelsPerEdge", &g_terrain.primitivePixelLengthTarget, 1, 32)) {
                configureTerrainPrograms();
            }
            if (ImGui::SliderFloat("DmapScale", &g_terrain.dmap.scale, 0.f, 1.f)) {
                configureTerrainPrograms();
                configureTopViewProgram();
            }
            if (ImGui::SliderFloat("LodStdev", &g_terrain.minLodStdev, 0.f, 0.005f, "%.4f")) {
                configureTerrainPrograms();
            }
            if (ImGui::SliderInt("MaxDepth", &g_terrain.maxDepth, 5, 29)) {
                loadBuffers();
                loadPrograms();
            }
        }
        ImGui::End();

        //ImGui::ShowDemoWindow();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // screen recording
    if (g_app.recorder.on) {
        char name[64], path[1024];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_BACK]);
        sprintf(name, "capture_%02i_%09i",
                g_app.recorder.capture,
                g_app.recorder.frame);
        strcat2(path, g_app.dir.output, name);
        djgt_save_glcolorbuffer_bmp(GL_BACK, GL_RGB, path);
        ++g_app.recorder.frame;
    }
}

// -----------------------------------------------------------------------------
/**
 * Render Everything
 *
 */
void render()
{
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glViewport(0, 0, g_framebuffer.w, g_framebuffer.h);
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderScene();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_app.viewer.w, g_app.viewer.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    renderViewer();


    ++g_app.frame;
}

////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
void
keyboardCallback(
    GLFWwindow*,
    int key, int, int action, int
) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;

    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            g_app.viewer.hud = !g_app.viewer.hud;
            break;
        case GLFW_KEY_C:
            if (g_app.recorder.on) {
                g_app.recorder.frame = 0;
                ++g_app.recorder.capture;
            }
            g_app.recorder.on = !g_app.recorder.on;
            break;
        case GLFW_KEY_R:
            loadBuffers();
            loadPrograms();
            break;
        default: break;
        }
    }
}

void mouseButtonCallback(GLFWwindow*, int, int, int)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}

void mouseMotionCallback(GLFWwindow* window, double x, double y)
{
    static double x0 = 0, y0 = 0;
    double dx = x - x0, dy = y - y0;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        dja::mat3 axis = dja::transpose(g_camera.axis);
        g_camera.axis = dja::mat3::rotation(dja::vec3(0, 0, 1), dx * 5e-3)
            * g_camera.axis;
        g_camera.axis = dja::mat3::rotation(axis[1], dy * 5e-3)
            * g_camera.axis;
        g_camera.axis[0] = dja::normalize(g_camera.axis[0]);
        g_camera.axis[1] = dja::normalize(g_camera.axis[1]);
        g_camera.axis[2] = dja::normalize(g_camera.axis[2]);

        g_camera.upAngle-= dx * 5e-3;
        g_camera.sideAngle+= dy * 5e-3;
        updateCameraMatrix();
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        dja::mat3 axis = dja::transpose(g_camera.axis);
        g_camera.pos -= axis[1] * dx * 5e-3 * norm(g_camera.pos);
        g_camera.pos += axis[2] * dy * 5e-3 * norm(g_camera.pos);
    }

    x0 = x;
    y0 = y;
}

void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (io.WantCaptureMouse)
        return;

    dja::mat3 axis = dja::transpose(g_camera.axis);
    g_camera.pos -= axis[0] * yoffset * 5e-2 * norm(g_camera.pos);
}

void usage(const char *app)
{
    printf("%s -- OpenGL Terrain Renderer\n", app);
    printf("usage: %s --shader-dir path_to_shader_dir\n", app);
}

// -----------------------------------------------------------------------------
int main(int, char **)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    // Create the Window
    LOG("Loading {Window-Main}\n");
    GLFWwindow* window = glfwCreateWindow(
        VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
        "Longest Edge Bisection Demo", NULL, NULL
    );
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
        log_debug_output();
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window, false);
        ImGui_ImplOpenGL3_Init("#version 450");
        LOG("-- Begin -- Init\n");
        init();
        LOG("-- End -- Init\n");

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            render();

            glfwSwapBuffers(window);
        }

        release();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
    }
    catch (std::exception& e) {
        LOG("%s", e.what());
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    catch (...) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    LOG("-- End -- Demo\n");

    return 0;
}
