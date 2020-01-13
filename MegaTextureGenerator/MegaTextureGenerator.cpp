//////////////////////////////////////////////////////////////////////////////
//
// This program creates a MegaTexture out of a displacement map.
//
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"


#define TT_IMPLEMENTATION
#include "TeraTexture.h"

#define LEB_IMPLEMENTATION
#include "LongestEdgeBisection.h"

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
#define BUFFER_SIZE(x)  sizeof(x) / sizeof(x[0]);

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
    DETAIL_MAP_SAND,
    DETAIL_MAP_GRASS,
    DETAIL_MAP_ROCK,
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
    struct {
        int size, pageSize;
    } output;
} g_textureGenerator = {
    {PATH_TO_ASSET_DIRECTORY "./kauai.png", 52660.0f, 52660.0f, -14.0f, 1587.0f},
    {
        {
            PATH_TO_ASSET_DIRECTORY "./sand_01_bump_4k.jpg",
            PATH_TO_ASSET_DIRECTORY "./sand_01_diff_4k.jpg",
            3.0f, 3.0f, 0.0f, 0.00f
        }, {
            PATH_TO_ASSET_DIRECTORY "./ForestFloor-06_BUMP_4k.jpg",
            PATH_TO_ASSET_DIRECTORY "./ForestFloor-06_COLOR_4k.jpg",
            3.0f, 3.0f, 0.0f, 0.05f
        }, {
            PATH_TO_ASSET_DIRECTORY "./ROCK-13_BUMP_4k.jpg",
            PATH_TO_ASSET_DIRECTORY "./ROCK-13_COLOR_4k.jpg",
            3.0f, 3.0f, 0.0f, 0.7f
        },
    },
    { 12, 10 }
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

    TEXTURE_DMAP_SAND,
    TEXTURE_DMAP_GRASS,
    TEXTURE_DMAP_ROCK,
    TEXTURE_AMAP_SAND,
    TEXTURE_AMAP_GRASS,
    TEXTURE_AMAP_ROCK,

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
        GLuint *glt = &g_gl.textures[TEXTURE_DMAP_SAND + i];
        djg_texture *djt = djgt_create(0);
        djgt_push_image_u8(djt, g_textureGenerator.detailsMaps[i].pathToDmap, true);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_SAND + i);
        djgt_to_gl(djt, GL_TEXTURE_2D, GL_R8, true, true, glt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        djgt_release(djt);
    }
    for (int i = 0; i < DETAIL_MAP_COUNT; ++i) {
        LOG("Loading {Amap-Detail-Texture}\n");
        GLuint *glt = &g_gl.textures[TEXTURE_AMAP_SAND + i];
        djg_texture *djt = djgt_create(0);
        djgt_push_image_u8(djt, g_textureGenerator.detailsMaps[i].pathToAmap, true);
        glActiveTexture(GL_TEXTURE0 + TEXTURE_AMAP_SAND + i);
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
    std::vector<float> dmap(w * h * 4);

    // store height
    for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
        uint16_t z = texels[i + w * j]; // in [0,2^16-1]
        float zf = float(z) / float((1 << 16) - 1);

        dmap[4 * (i + w * j)] = zf;
    }

    // compute slopes (temporarily)
    for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
        int i1 = std::max(i - 1, 0);
        int i2 = std::min(i + 1, w - 1);
        int j1 = std::max(j - 1, 0);
        int j2 = std::min(j + 1, h - 1);
        float x1 = dmap[4 * (i1 + w * j)];
        float x2 = dmap[4 * (i2 + w * j)];
        float y1 = dmap[4 * (i + w * j1)];
        float y2 = dmap[4 * (i + w * j2)];
        float xSlope = 0.5f * (x2 - x1) * (float)w;
        float ySlope = 0.5f * (y2 - y1) * (float)w;

        dmap[1 + 4 * (i + w * j)] = xSlope;
        dmap[2 + 4 * (i + w * j)] = ySlope;
    }

    // compute curvature
    for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
        int i1 = std::max(i - 1, 0);
        int i2 = std::min(i + 1, w - 1);
        int j1 = std::max(j - 1, 0);
        int j2 = std::min(j + 1, h - 1);
        float dpdx1 = dmap[1 + 4 * (i1 + w * j)];
        float dpdx2 = dmap[1 + 4 * (i2 + w * j)];
        //float dpdy1 = dmap[1 + 4 * (i + w * j1)];
        //float dpdy2 = dmap[1 + 4 * (i + w * j2)];
        //float dqdx1 = dmap[2 + 4 * (i1 + w * j)];
        //float dqdx2 = dmap[2 + 4 * (i2 + w * j)];
        float dqdy1 = dmap[2 + 4 * (i + w * j1)];
        float dqdy2 = dmap[2 + 4 * (i + w * j2)];
        float dpdx = 0.5f * (dpdx2 - dpdx1) * (float)(w);
        //float dpdy = 0.5f * (dpdy2 - dpdy1) * (float)(h);
        //float dqdx = 0.5f * (dqdx2 - dqdx1) * (float)(w);
        float dqdy = 0.5f * (dqdy2 - dqdy1) * (float)(h);
        float curvature = dpdx + dqdy / 2.0f;

        dmap[3 + 4 * (i + w * j)] = curvature;
    }

    // store curvature in second channel
    for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
        dmap[1 + 4 * (i + w * j)] = dmap[3 + 4 * (i + w * j)];
    }

    glGenTextures(1, glt);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP_TERRAIN);
    glBindTexture(GL_TEXTURE_2D, *glt);
    glTexStorage2D(GL_TEXTURE_2D, mipcnt, GL_RGBA32F, w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, &dmap[0]);
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
    LOG("Loading {Preview-Program}\n");
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
            TEXTURE_AMAP_SAND,
            TEXTURE_AMAP_GRASS,
            TEXTURE_AMAP_ROCK
        };
        GLint displacementLocations[] = {
            TEXTURE_DMAP_SAND,
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
    glDeleteTextures(TEXTURE_COUNT, g_gl.textures);
    glDeleteBuffers(BUFFER_COUNT, g_gl.buffers);
    for (int i = 0; i < PROGRAM_COUNT; ++i)
        glDeleteProgram(g_gl.programs[i]);
    glDeleteVertexArrays(1, &g_gl.vertexArray);
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
void ExportTexture();

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
        ImGui::Text("Export Settings");
        ImGui::SliderInt("Size", &g_textureGenerator.output.size, 10, 20);
        ImGui::SliderInt("PageSize", &g_textureGenerator.output.pageSize, 1, 12);
        if (ImGui::Button("Generate")) {
            ExportTexture();
        }
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// -----------------------------------------------------------------------------
enum {
    TEXTURE_EXPORT_PAGE_ALBEDO_RAW,
    TEXTURE_EXPORT_PAGE_NORMAL_RAW,
    TEXTURE_EXPORT_PAGE_ALBEDO,
    TEXTURE_EXPORT_PAGE_NORMAL,
    TEXTURE_EXPORT_PAGE_DISPLACEMENT,

    TEXTURE_EXPORT_COUNT
};

GLuint LoadExportTexture(int textureID, GLenum internalformat, int size)
{
    GLuint texture;

    glActiveTexture(GL_TEXTURE0 + TEXTURE_COUNT + textureID);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, size, internalformat, 1 << size, 1 << size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);

    return texture;
}

GLuint LoadFramebuffer(GLuint albedo, GLuint displacement, GLuint normal)
{
    GLuint framebuffer;
    const GLenum drawBuffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2
    };

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           albedo,
                           0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D,
                           displacement,
                           0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT2,
                           GL_TEXTURE_2D,
                           normal,
                           0);
    glDrawBuffers(3, drawBuffers);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return framebuffer;
}

GLuint LoadGenerationProgram()
{
    djg_program *djgp = djgp_create();
    GLuint program;

    djgp_push_string(djgp,
                     "#define WORLD_SPACE_TEXTURE_DIMENSIONS_BUFFER_BINDING %i\n",
                     BUFFER_TEXTURE_DIMENSIONS);
    djgp_push_file(djgp, PATH_TO_NOISE_GLSL_LIBRARY "gpu_noise_lib.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/TerrainTexture.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/LongestEdgeBisection.glsl");
    djgp_push_file(djgp, PATH_TO_SRC_DIRECTORY "./shaders/TextureGeneration.glsl");
    djgp_to_gl(djgp, 450, false, true, &program);
    djgp_release(djgp);

    glUseProgram(program);
    {
        GLint albedoLocations[] = {
            TEXTURE_AMAP_SAND,
            TEXTURE_AMAP_GRASS,
            TEXTURE_AMAP_ROCK
        };
        GLint displacementLocations[] = {
            TEXTURE_DMAP_SAND,
            TEXTURE_DMAP_GRASS,
            TEXTURE_DMAP_ROCK
        };
        glUniform1i(glGetUniformLocation(program, "TT_TerrainDisplacementSampler"),
                    TEXTURE_DMAP_TERRAIN);
        glUniform1iv(glGetUniformLocation(program, "TT_DetailAlbedoSamplers"),
                     DETAIL_MAP_COUNT,
                     albedoLocations);
        glUniform1iv(glGetUniformLocation(program, "TT_DetailDisplacementSamplers"),
                     DETAIL_MAP_COUNT,
                     displacementLocations);
    }
    glUseProgram(0);

    return program;
}

void ExportTexture()
{
    int textureRes = g_textureGenerator.output.size;
    int pageRes = g_textureGenerator.output.pageSize;
    struct {
        struct {
            uint8_t *data;
            int byteSize;
        } albedo, normal, displacement;
    } rawTextureData, textureData;
    GLuint textures[TEXTURE_EXPORT_COUNT];
    GLuint framebuffer, program;
    tt_Texture *tt;

    // init OpenGL resources
    textures[TEXTURE_EXPORT_PAGE_ALBEDO_RAW] = LoadExportTexture(TEXTURE_EXPORT_PAGE_ALBEDO,
                                                                 GL_RGBA8,
                                                                 pageRes);
    textures[TEXTURE_EXPORT_PAGE_ALBEDO] = LoadExportTexture(TEXTURE_EXPORT_PAGE_ALBEDO,
                                                             GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                                             pageRes);
    textures[TEXTURE_EXPORT_PAGE_NORMAL_RAW] = LoadExportTexture(TEXTURE_EXPORT_PAGE_NORMAL,
                                                                 GL_RG8,
                                                                 pageRes);
    textures[TEXTURE_EXPORT_PAGE_NORMAL] = LoadExportTexture(TEXTURE_EXPORT_PAGE_NORMAL,
                                                             GL_COMPRESSED_RED_GREEN_RGTC2_EXT,
                                                             pageRes);
    textures[TEXTURE_EXPORT_PAGE_DISPLACEMENT] = LoadExportTexture(TEXTURE_EXPORT_PAGE_DISPLACEMENT,
                                                                   GL_R16,
                                                                   pageRes);
    framebuffer = LoadFramebuffer(textures[TEXTURE_EXPORT_PAGE_ALBEDO_RAW],
                                  textures[TEXTURE_EXPORT_PAGE_DISPLACEMENT],
                                  textures[TEXTURE_EXPORT_PAGE_NORMAL_RAW]);
    program = LoadGenerationProgram();

    // create the tt_Texture file
    TT_LOG("Creating texture file...");
    int64_t pageResolutions[] = {pageRes, pageRes, pageRes - 2};
    tt_Format formats[]   = {/*albedo*/TT_FORMAT_BC1,
                             /*normals*/TT_FORMAT_BC5,
                             /*displacement*/TT_FORMAT_R16};
    tt_CreateLayered("texture.tt", textureRes, 3, pageResolutions, formats);
    tt = tt_Load("texture.tt", /* cache (not important here) */16);

    // allocate memory for raw data
    rawTextureData.albedo.byteSize          = (1 << (2 * pageResolutions[0])) * /* RGBA8 */4;
    rawTextureData.normal.byteSize          = (1 << (2 * pageResolutions[1])) * /* RG8 */2;
    rawTextureData.displacement.byteSize    = (1 << (2 * pageResolutions[2])) * /* R16 */2;
    rawTextureData.albedo.data              = (uint8_t *)malloc(rawTextureData.albedo.byteSize);
    rawTextureData.normal.data              = (uint8_t *)malloc(rawTextureData.normal.byteSize);
    rawTextureData.displacement.data        = (uint8_t *)malloc(rawTextureData.displacement.byteSize);

    // allocate memory for compressed data
    glGetTextureLevelParameteriv(textures[TEXTURE_EXPORT_PAGE_ALBEDO], 0,
                                 GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                                 &textureData.albedo.byteSize);
    textureData.albedo.data = (uint8_t *)malloc(textureData.albedo.byteSize);
    glGetTextureLevelParameteriv(textures[TEXTURE_EXPORT_PAGE_NORMAL], 0,
                                 GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                                 &textureData.normal.byteSize);
    textureData.normal.data = (uint8_t *)malloc(textureData.normal.byteSize);

    // create pages and write to disk
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, 1 << pageRes, 1 << pageRes);
    glUseProgram(program);
    glBindVertexArray(g_gl.vertexArray);
    for (int64_t i = 0; i < (2 << tt->storage.header.depth); ++i) {
        TT_LOG("Generating page %li / %i", i + 1, (2 << tt->storage.header.depth));
        int64_t pageSize = textureData.albedo.byteSize
                         + textureData.normal.byteSize
                         + rawTextureData.displacement.byteSize;

        // write to raw data
        glUniform1ui(glGetUniformLocation(program, "u_NodeID"), i);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glGenerateTextureMipmap(textures[TEXTURE_EXPORT_PAGE_ALBEDO_RAW]);
        glGenerateTextureMipmap(textures[TEXTURE_EXPORT_PAGE_DISPLACEMENT]);
        glGenerateTextureMipmap(textures[TEXTURE_EXPORT_PAGE_NORMAL_RAW]);

        // retrieve raw data from the GPU
        glGetTextureImage(textures[TEXTURE_EXPORT_PAGE_ALBEDO_RAW],
                          0, GL_RGBA, GL_UNSIGNED_BYTE,
                          rawTextureData.albedo.byteSize,
                          rawTextureData.albedo.data);
        glGetTextureImage(textures[TEXTURE_EXPORT_PAGE_NORMAL_RAW],
                          0, GL_RG, GL_UNSIGNED_BYTE,
                          rawTextureData.normal.byteSize,
                          rawTextureData.normal.data);
        glGetTextureImage(textures[TEXTURE_EXPORT_PAGE_DISPLACEMENT],
                          pageResolutions[0] - pageResolutions[2],
                          GL_RED, GL_UNSIGNED_SHORT,
                          rawTextureData.displacement.byteSize,
                          rawTextureData.displacement.data);

        // compress raw data on the GPU
        glTextureSubImage2D(textures[TEXTURE_EXPORT_PAGE_ALBEDO],
                            0, 0, 0, 1 << pageResolutions[0], 1 << pageResolutions[0],
                            GL_RGBA, GL_UNSIGNED_BYTE,
                            rawTextureData.albedo.data);
        glTextureSubImage2D(textures[TEXTURE_EXPORT_PAGE_NORMAL],
                            0, 0, 0, 1 << pageResolutions[1], 1 << pageResolutions[0],
                            GL_RG, GL_UNSIGNED_BYTE,
                            rawTextureData.normal.data);
        glGetCompressedTextureImage(textures[TEXTURE_EXPORT_PAGE_ALBEDO], 0,
                                    textureData.albedo.byteSize,
                                    textureData.albedo.data);
        glGetCompressedTextureImage(textures[TEXTURE_EXPORT_PAGE_NORMAL], 0,
                                    textureData.normal.byteSize,
                                    textureData.normal.data);

        // write compressed data to disk
        fseek(tt->storage.stream,
              sizeof(tt__Header) + (uint64_t)pageSize * (uint64_t)i,
              SEEK_SET);
        fwrite(textureData.albedo.data,
               textureData.albedo.byteSize,
               1, tt->storage.stream);
        fwrite(textureData.normal.data,
               textureData.normal.byteSize,
               1, tt->storage.stream);
        fwrite(rawTextureData.displacement.data,
               rawTextureData.displacement.byteSize,
               1, tt->storage.stream);
    }
    glBindVertexArray(0);
    glUseProgram(0);

    TT_LOG("Wrote %f GiB to disk", (float)tt_StorageSize(tt) / (1024.f * 1024.f * 1024.f));

    // release OpenGL objects
    glDeleteTextures(TEXTURE_EXPORT_COUNT, textures);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteProgram(program);

    // release memory
    tt_Release(tt);
    free(textureData.albedo.data);
    free(textureData.normal.data);
    free(rawTextureData.albedo.data);
    free(rawTextureData.displacement.data);
    free(rawTextureData.normal.data);
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
    GLFWwindow* window = glfwCreateWindow(VIEWPORT_WIDTH+256, VIEWPORT_WIDTH, "Terrain Generator", NULL, NULL);
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
    LOG("-- End -- Demo\n");

    return 0;
}


