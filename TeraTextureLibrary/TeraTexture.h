
#ifndef TT_INCLUDE_TT_H
#define TT_INCLUDE_TT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TT_STATIC
#define TTDEF static
#else
#define TTDEF extern
#endif

// supported formats
enum tt_Format {
    TT_FORMAT_RGB,  // RGB unsigned byte
    TT_FORMAT_HDR,  // RGB 16-bit floating point
    TT_FORMAT_PBR   // displacement + normals + albedo
};

// projection type
enum tt_Projection {
    TT_PROJECTION_ORTHOGRAPHIC  = 0,
    TT_PROJECTION_PERSPECTIVE   = 1
};

// update arguments
typedef struct {
    struct {
        float modelView[16];                // column-major modelView matrix
        float modelViewProjection[16];      // column-major modelViewProjection matrix
    } matrices;
    struct {
        int width, height;                  // framebuffer resolution (in pixels)
    } framebuffer;
    struct {
        float width, height;                // projection plane in world space units
    } worldSpaceImagePlaneAtUnitDepth;
    tt_Projection projection;               // projection type
    float pixelsPerTexelTarget;             // target pixels per texel density
} tt_UpdateArgs;

// data type
typedef struct tt_Texture tt_Texture;

// create a file
TTDEF bool tt_Create(const char *file, tt_Format format, int size, int pageSize);

// ctor / dtor
TTDEF tt_Texture *tt_Load(const char *filename, int cacheCapacity);
TTDEF void tt_Release(tt_Texture *tt);

// update
TTDEF void tt_Update(tt_Texture *tt, const tt_UpdateArgs *args);

// raw OpenGL accessors
TTDEF GLuint tt_LebBuffer(const tt_Texture *tt);
TTDEF GLuint tt_IndirectionBuffer(const tt_Texture *tt);

// queries
TTDEF int tt_TexturesPerPage(const tt_Texture *tt);

// texture binding
TTDEF void tt_BindPageTextures(const tt_Texture *tt, GLenum *textureUnits);

#ifdef __cplusplus
} // extern "C"
#endif

//
//
//// end header file ///////////////////////////////////////////////////////////
#endif // TT_INCLUDE_TT_H

#include <math.h>
#include "uthash.h"
#include "LongestEdgeBisection.h"

#ifndef TT_ASSERT
#    include <assert.h>
#    define TT_ASSERT(x) assert(x)
#endif

#ifndef TT_LOG
#    include <stdio.h>
#    define TT_LOG(format, ...) do { fprintf(stdout, format "\n", ##__VA_ARGS__); fflush(stdout); } while(0)
#endif

#ifndef TT_MALLOC
#    include <stdlib.h>
#    define TT_MALLOC(x) (malloc(x))
#    define TT_FREE(x) (free(x))
#else
#    ifndef TT_FREE
#        error TT_MALLOC defined without TT_FREE
#    endif
#endif

#define TT__BUFFER_SIZE(x)   ((int)(sizeof(x) / sizeof(x[0])))
#define TT__BUFFER_OFFSET(i) ((char *)NULL + (i))

#define TT__UPDATER_STREAM_BUFFER_BYTE_SIZE   (1 << 26)


/*******************************************************************************
 * Texture Page Data Structure
 *
 * The tt_Texture caches a small set of texture pages into GPU memory using
 * OpenGL texture objects. This data structure tells which OpenGL texture
 * represents a given page.
 *
 */
typedef struct {
    uint32_t key;
    int32_t textureID;

    UT_hash_handle hh;
} tt__Page;


/*******************************************************************************
 * Texture Data Structure
 *
 * This is the main data structure of the library. It consists of 3 different
 * modules: the storage, the cache, and the updater. The storage describes the
 * where the entire texture is stored in memory. The cache is responsible for
 * storing the parts of the texture that are stored in GPU memory. Finally,
 * the updater is responsible for updating the cache, by loading necessary
 * pages into memory.
 *
 */
struct tt_Texture {
    struct {
        FILE *stream;               // pointer to file
        struct {
            int size;               // side resolution of the pages
            enum tt_Format format;  // format of the pages
        } pages;
        int depth;                  // max LEB subdivision depth
    } storage;

    struct {
        tt__Page *pages;            // LRU map
        leb_Heap *leb;              // LEB
        struct {
            GLuint      *textures;  // texture names
            GLuint      *buffers;   // handles (UBO) + LEB Heap (SSBO)
        } gl;
        int capacity;               // capacity in number of pages
    } cache;

    struct {
        struct {
            GLuint *programs;       // compute kernel that updates the LEB
            GLuint *buffers;        // DISPATCH INDIRECT, LEB RW, LEB STREAM, PIXEL UNPACK, XFORM
            GLuint *queries;        // TIMESTAMP
        } gl;
        GLint isReady;              // asynchronous flage
        GLint splitOrMerge;         // LEB update tracking
        GLint streamByteOffset;     //
    } updater;
};


/*******************************************************************************
 * Header File Data Structure
 *
 * This represents the header we use to uniquely identify the tt_Texture files
 * and provide the fundamental information to properly decode the rest of the
 * file.
 *
 */
typedef struct {
    int32_t magic;          // integer value
    int32_t format;         // texture format
    int32_t depth;          // LEB depth
    int32_t pageSize;       // page resolution
} tt__Header;


/*******************************************************************************
 * Update Parameters Data Structure
 *
 * This data structure holds the parameters used to update the cache on the GPU.
 *
 */
typedef struct {
    float modelView[16];        // modelview transformation matrix
    struct {
        float x, y, z, w;
    } frustumPlanes[6];         // frustum planes
    float lodFactor[2];         // constants for LoD calculation
    float align[24];            // align to power of two
} tt__UpdateParameters;


/*******************************************************************************
 * Magic -- Generates the magic identifier
 *
 * Each tt_Texture file starts with 4 Bytes that allow us to check if the file
 * under reading is actually a tt_Texture file.
 *
 */
static int32_t tt__Magic()
{
    const union {
        char    string[4];
        int32_t numeric;
    } magic = {.string = {'t', 't', '0', '1'}};

    return magic.numeric;
}


/*******************************************************************************
 * SizeToLebDepth -- Computes the LEB depth given a texture size
 *
 * The tessellation level of a side of the domain is given by
 * T = 2^[(D - 1)/2]. Inverting the formula yields D = 2 lg(T) + 1,
 * which is what this function returns.
 *
 */
static int32_t tt__SizeToLebDepth(int32_t size, int32_t pageSize)
{
    return 2 * (size - pageSize) + 1;
}


/*******************************************************************************
 * PageCount -- Computes the number of pages stored in the tt_Texture file
 *
 */
static int32_t tt__PageCount(int32_t lebDepth)
{
    return 2 << lebDepth;
}


/*******************************************************************************
 * PageCount -- Computes the number of pages stored in the tt_Texture file
 *
 */
static size_t tt__BytesPerPage(tt_Format format, int32_t pageSize)
{
    size_t texelCount = 1u << (2 * pageSize);

    switch (format) {
    case TT_FORMAT_RGB:
        return texelCount >> 1; // BC1: 1/2 Byte per texel
    case TT_FORMAT_HDR:
        return texelCount;      // BC6: 1 Byte per texel
    case TT_FORMAT_PBR:
        return  /* BC1 */ (texelCount >> 1) +
                /* BC6 */ (texelCount) +
                /* raw u16 (x2) */ (texelCount << 2);
    }
}


/*******************************************************************************
 * Create -- Allocates storage for a tt_Texture file
 *
 * This procedure creates a file on disk that stores the data. The function
 * returns true if the file is successfully created, and false otherwise.
 * The format describes the format of the texture.
 * The xy describes the resolution of the texture in base 2 logarithm.
 * The zw describes the resolution of the pages in base 2 logarithm.
 *
 */
TTDEF bool tt_Create(const char *file, tt_Format format, int size, int pageSize)
{
    TT_ASSERT(size > pageSize && "pageSize should be less than size");

    int lebDepth = tt__SizeToLebDepth(size, pageSize);
    tt__Header header = { tt__Magic(), format, lebDepth, pageSize };
    size_t bytesPerPage = tt__BytesPerPage(format, pageSize);
    uint8_t *pageData = (uint8_t *)TT_MALLOC(bytesPerPage);
    int pageCount = tt__PageCount(lebDepth);
    FILE *stream = fopen(file, "wb");


    if (!stream) {
        TT_LOG("tt_Texture: fopen failed");

        return false;
    }

    if (lebDepth >= 28) {
        TT_LOG("tt_Texture: unsupported resolution");
        fclose(stream);

        return false;
    }

    if (fwrite(&header, sizeof(header), 1, stream) != 1) {
        TT_LOG("tt_Texture: header dump failed");
        fclose(stream);

        return false;
    }

    memset(pageData, 0, bytesPerPage);
    for (int i = 0; i < pageCount; ++i) {
        if (fwrite(pageData, bytesPerPage, 1, stream) != 1) {
            TT_LOG("tt_Texture: page dump failed");
            fclose(stream);

            return false;
        }
    }
    TT_FREE(pageData);

    fclose(stream);

    TT_LOG("tt_Texture: wrote %.1f MiBytes to disk",
           (float)(sizeof(header) + pageCount * bytesPerPage) / (float)(1024 * 1024));

    return true;
}


/*******************************************************************************
 * ReadHeader -- Reads a tt_Texture file header from an input stream
 *
 */
static bool tt__ReadHeader(FILE *stream, tt__Header *header)
{
    if (fread(header, sizeof(*header), 1, stream) != 1) {
        TT_LOG("tt_Texture: fread failed");

        return false;
    }

    return header->magic == tt__Magic();
}


/*******************************************************************************
 * LoadStorage -- Loads a tt_Texture storage component
 *
 */
static void
tt__LoadStorage(tt_Texture *tt, const tt__Header header, FILE *stream)
{
    tt->storage.stream = stream;
    tt->storage.pages.size = header.pageSize;
    tt->storage.pages.format = (tt_Format)header.format;
    tt->storage.depth = header.depth;
}


/*******************************************************************************
 * ReleaseStorage --Releases the storage component of a tt_Texture
 *
 */
static void tt__ReleaseStorage(tt_Texture *tt)
{
    fclose(tt->storage.stream);
}


/*******************************************************************************
 * TexturesPerPage -- Determines the number of OpenGL texture per page
 *
 */
TTDEF int tt_TexturesPerPage(const tt_Texture *tt)
{
    switch (tt->storage.pages.format) {
    case TT_FORMAT_HDR:
    case TT_FORMAT_RGB:
        return 1;
    case TT_FORMAT_PBR:
        return 3;
    }
}


/*******************************************************************************
 * LoadCacheTextures -- Allocates GPU texture memory for the cache
 *
 */
static bool tt__LoadCacheTextures(tt_Texture *tt)
{
    int textureSize = 1 << tt->storage.pages.size;
    int texturesPerPage = tt_TexturesPerPage(tt);
    GLuint *textures = (GLuint *)TT_MALLOC(sizeof(GLuint) * texturesPerPage);

    TT_LOG("tt_Texture: allocating %lu MiBytes of GPU memory using %i texture(s)",
           (tt->cache.capacity * texturesPerPage * tt__BytesPerPage(tt->storage.pages.format, tt->storage.pages.size)) >> 20,
           texturesPerPage);
    glGenTextures(texturesPerPage, textures);

    for (int i = 0; i < texturesPerPage; ++i) {
        GLuint *texture = &textures[i];

        glBindTexture(GL_TEXTURE_2D_ARRAY, *texture);

        glTextureStorage3D(*texture, 1, GL_RGBA8, textureSize, textureSize,
                           tt->cache.capacity);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(*texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    if (glGetError() != GL_NO_ERROR) {
        TT_FREE(textures);

        return false;
    }

    tt->cache.gl.textures = textures;

    return true;
}


/*******************************************************************************
 * ReleaseCacheTextures -- Cleans up GPU texture memory
 *
 */
static void tt__ReleaseCacheTextures(tt_Texture *tt)
{
    glDeleteTextures(tt_TexturesPerPage(tt), tt->cache.gl.textures);
    TT_FREE(tt->cache.gl.textures);
}


/*******************************************************************************
 * Enumeration of the GPU buffers used for cache management
 *
 */
enum {
    TT__CACHE_GL_BUFFER_LEB,    // LEB heap
    TT__CACHE_GL_BUFFER_MAP,    // mapping from nodeID to textureID

    TT__CACHE_GL_BUFFER_COUNT,
};


/*******************************************************************************
 * LoadCacheBuffers -- Loads GPU buffer memory for the cache
 *
 */
static bool tt__LoadCacheBuffers(tt_Texture *tt)
{
    int bufferCount = TT__CACHE_GL_BUFFER_COUNT;
    GLuint *buffers = (GLuint *)TT_MALLOC(sizeof(GLuint) * bufferCount);

    glGenBuffers(bufferCount, buffers);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[TT__CACHE_GL_BUFFER_LEB]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    2 * sizeof(int32_t) + leb_HeapByteSize(tt->cache.leb),
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, buffers[TT__CACHE_GL_BUFFER_MAP]);
    glBufferStorage(GL_UNIFORM_BUFFER,
                    sizeof(GLint) * tt->cache.capacity,
                    NULL,
                    0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(TT__CACHE_GL_BUFFER_COUNT, buffers);
        TT_FREE(buffers);

        return false;
    }

    tt->cache.gl.buffers = buffers;

    return true;
}


/*******************************************************************************
 * ReleaseCacheBuffers -- Releases the GPU buffer memory for the cache
 *
 */
static void tt__ReleaseCacheBuffers(tt_Texture *tt)
{
    glDeleteBuffers(TT__CACHE_GL_BUFFER_COUNT, tt->cache.gl.buffers);
    TT_FREE(tt->cache.gl.buffers);
}


/*******************************************************************************
 * LoadCache -- Loads the cache component of a tt_Texture
 *
 */
static bool tt__LoadCache(tt_Texture *tt, int cachePageCapacity)
{
    tt->cache.pages = NULL;
    tt->cache.leb = leb_CreateMinMax(1, tt->storage.depth);
    tt->cache.capacity = cachePageCapacity;

    if (!tt__LoadCacheTextures(tt)) {
        return false;
    }

    if (!tt__LoadCacheBuffers(tt)) {
        tt__ReleaseCacheTextures(tt);

        return false;
    }

    return true;
}


/*******************************************************************************
 * ReleaseCache -- Releases the cache component of a tt_Texture
 *
 */
static void tt__ReleaseCache(tt_Texture *tt)
{
    tt__Page *page, *tmp;

    HASH_ITER(hh, tt->cache.pages, page, tmp) {
        HASH_DELETE(hh, tt->cache.pages, page);
        free(page);
    }

    leb_Release(tt->cache.leb);
    tt__ReleaseCacheBuffers(tt);
    tt__ReleaseCacheTextures(tt);
}


/*******************************************************************************
 * Enumeration of the GPU buffers used for the cache updater
 *
 */
enum {
    TT__UPDATER_GL_BUFFER_DISPATCH,
    TT__UPDATER_GL_BUFFER_LEB_CPU,
    TT__UPDATER_GL_BUFFER_LEB_GPU,
    TT__UPDATER_GL_BUFFER_MAP,
    TT__UPDATER_GL_BUFFER_PARAMETERS,
    TT__UPDATER_GL_BUFFER_STREAM,

    TT__UPDATER_GL_BUFFER_COUNT
};


/*******************************************************************************
 * LoadUpdaterBuffers -- Loads the GPU buffers for updating the tt_Texture cache
 *
 */
static bool tt__LoadUpdaterBuffers(tt_Texture *tt)
{
    int bufferCount = TT__UPDATER_GL_BUFFER_COUNT;
    GLuint *buffers = (GLuint *)TT_MALLOC(sizeof(GLuint) * bufferCount);
    const uint32_t dispatchData[8] = {
        leb_NodeCount(tt->cache.leb) / 256u + 1, 1, 1,
        0, 0, 0, 0, 0
    };
    int lebBufferSize = leb_HeapByteSize(tt->cache.leb) + 2 * sizeof(int32_t);
    uint32_t *lebBufferData = (uint32_t *)TT_MALLOC(lebBufferSize);

    lebBufferData[0] = leb_MinDepth(tt->cache.leb);
    lebBufferData[1] = leb_MaxDepth(tt->cache.leb);
    memcpy(&lebBufferData[2],
           leb_GetHeapMemory(tt->cache.leb),
           leb_HeapByteSize(tt->cache.leb));

    glGenBuffers(bufferCount, buffers);

    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_DISPATCH]);
    glBufferStorage(GL_DISPATCH_INDIRECT_BUFFER,
                    sizeof(dispatchData),
                    dispatchData,
                    0);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);

    glBindBuffer(GL_COPY_READ_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_LEB_CPU]);
    glBufferStorage(GL_COPY_READ_BUFFER,
                    leb_HeapByteSize(tt->cache.leb) + 2 * sizeof(GLint),
                    NULL,
                    GL_MAP_READ_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_LEB_GPU]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    leb_HeapByteSize(tt->cache.leb) + 2 * sizeof(GLint),
                    lebBufferData,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_COPY_READ_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_MAP]);
    glBufferStorage(GL_COPY_READ_BUFFER,
                    sizeof(GLint) * tt->cache.capacity,
                    NULL,
                    GL_MAP_WRITE_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_PARAMETERS]);
    glBufferStorage(GL_UNIFORM_BUFFER,
                    sizeof(tt__UpdateParameters),
                    NULL,
                    0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_STREAM]);
    glBufferStorage(GL_PIXEL_UNPACK_BUFFER,
                    TT__UPDATER_STREAM_BUFFER_BYTE_SIZE,
                    NULL,
                    GL_MAP_WRITE_BIT);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(TT__UPDATER_GL_BUFFER_COUNT, buffers);
        TT_FREE(lebBufferData);
        TT_FREE(buffers);

        return false;
    }

    tt->updater.gl.buffers = buffers;

    TT_FREE(lebBufferData);

    return true;
}


/*******************************************************************************
 * ReleaseUpdaterBuffers -- Releases GPU buffers for updating the tt_Texture cache
 *
 */
static void tt__ReleaseUpdaterBuffers(tt_Texture *tt)
{
    glDeleteBuffers(TT__UPDATER_GL_BUFFER_COUNT, tt->updater.gl.buffers);

    TT_FREE(tt->updater.gl.buffers);
}


/*******************************************************************************
 * Enumeration of the GPU queries used for the cache updater
 *
 */
enum {
    TT__UPDATER_GL_QUERY_TIMESTAMP,

    TT__UPDATER_GL_QUERY_COUNT
};


/*******************************************************************************
 * LoadUpdaterQueries -- Loads the GPU queries for updating the tt_Texture cache
 *
 */
static bool tt__LoadUpdaterQueries(tt_Texture *tt)
{
    int queryCount = TT__UPDATER_GL_QUERY_COUNT;
    GLuint *queries = (GLuint *)TT_MALLOC(sizeof(GLuint) * queryCount);

    glGenQueries(TT__UPDATER_GL_QUERY_COUNT, queries);

    glQueryCounter(queries[TT__UPDATER_GL_QUERY_TIMESTAMP], GL_TIMESTAMP);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteQueries(TT__UPDATER_GL_QUERY_COUNT, queries);
        TT_FREE(queries);

        return false;
    }

    tt->updater.gl.queries = queries;

    return (glGetError() == GL_NO_ERROR);
}


/*******************************************************************************
 * ReleaseUpdaterQueries -- Releases GPU queries for updating the tt_Texture cache
 *
 */
static void tt__ReleaseUpdaterQueries(tt_Texture *tt)
{
    glDeleteQueries(TT__UPDATER_GL_QUERY_COUNT, tt->updater.gl.queries);

    TT_FREE(tt->updater.gl.queries);
}


/*******************************************************************************
 * Enumeration of the GPU programes used for the cache updater
 *
 */
enum {
    TT__UPDATER_GL_PROGRAM_DISPATCH,
    TT__UPDATER_GL_PROGRAM_MERGE,
    TT__UPDATER_GL_PROGRAM_SPLIT,
    TT__UPDATER_GL_PROGRAM_REDUCTION,
    TT__UPDATER_GL_PROGRAM_REDUCTION_PREPASS,

    TT__UPDATER_GL_PROGRAM_COUNT
};

#define TT__STRINGIFY(x) #x
static const char *tt__LongestEdgeBisectionLibraryShaderSource()
{
    static const char *str =
        #include "LongestEdgeBisection.glsl.str"
    ;

    return str;
}

static const char *tt__LongestEdgeBisectionUpdateShaderSource()
{
    static const char *str =
        #include "LongestEdgeBisectionUpdate.glsl.str"
    ;

    return str;
}

static const char *tt__LongestEdgeBisectionDispatchShaderSource()
{
    static const char *str =
        #include "LongestEdgeBisectionDispatch.glsl.str"
    ;

    return str;
}

static const char *tt__LongestEdgeBisectionReductionShaderSource()
{
    static const char *str =
        #include "LongestEdgeBisectionReduction.glsl.str"
    ;

    return str;
}

static const char *tt__LongestEdgeBisectionReductionPrepassShaderSource()
{
    static const char *str =
        #include "LongestEdgeBisectionReductionPrepass.glsl.str"
    ;

    return str;
}
#undef TT__STRINGIFY


static void tt__LoadUpdaterProgramSplit(GLuint *program)
{
    char header[256];
    const char *strings[] = {
        "#version 450\n",
        header,
        "#define FLAG_SPLIT 1\n",
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__LongestEdgeBisectionUpdateShaderSource()
    };

    sprintf(header,
            "#define BUFFER_BINDING_LEB %i\n"
            "#define BUFFER_BINDING_PARAMETERS %i\n",
            TT__UPDATER_GL_BUFFER_LEB_GPU,
            TT__UPDATER_GL_BUFFER_PARAMETERS);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static void tt__LoadUpdaterProgramMerge(GLuint *program)
{
    char header[256];
    const char *strings[] = {
        "#version 450\n",
        header,
        "#define FLAG_MERGE 1\n",
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__LongestEdgeBisectionUpdateShaderSource()
    };

    sprintf(header,
            "#define BUFFER_BINDING_LEB %i\n"
            "#define BUFFER_BINDING_PARAMETERS %i\n",
            TT__UPDATER_GL_BUFFER_LEB_GPU,
            TT__UPDATER_GL_BUFFER_PARAMETERS);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static void tt__LoadUpdaterProgramDispatch(GLuint *program)
{
    char header[256];
    const char *strings[] = {
        "#version 450\n",
        header,
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__LongestEdgeBisectionDispatchShaderSource()
    };

    sprintf(header,
            "#define BUFFER_BINDING_DISPATCH_INDIRECT_COMMAND %i\n"
            "#define BUFFER_BINDING_LEB %i\n",
            TT__UPDATER_GL_BUFFER_DISPATCH,
            TT__UPDATER_GL_BUFFER_LEB_GPU);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static void tt__LoadUpdaterProgramReduction(GLuint *program)
{
    char header[256];
    const char *strings[] = {
        "#version 450\n",
        header,
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__LongestEdgeBisectionReductionShaderSource()
    };

    sprintf(header,
            "#define BUFFER_BINDING_LEB %i\n",
            TT__UPDATER_GL_BUFFER_LEB_GPU);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static void tt__LoadUpdaterProgramReductionPrepass(GLuint *program)
{
    char header[256];
    const char *strings[] = {
        "#version 450\n",
        header,
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__LongestEdgeBisectionReductionPrepassShaderSource()
    };

    sprintf(header,
            "#define BUFFER_BINDING_LEB %i\n",
            TT__UPDATER_GL_BUFFER_LEB_GPU);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static bool tt__LoadUpdaterPrograms(tt_Texture *tt)
{
    int programCount = TT__UPDATER_GL_PROGRAM_COUNT;
    GLuint *programs = (GLuint *)TT_MALLOC(sizeof(GLuint) * programCount);
    GLint areProgramsReady = GL_TRUE;

    tt__LoadUpdaterProgramDispatch(&programs[TT__UPDATER_GL_PROGRAM_DISPATCH]);
    tt__LoadUpdaterProgramMerge(&programs[TT__UPDATER_GL_PROGRAM_MERGE]);
    tt__LoadUpdaterProgramSplit(&programs[TT__UPDATER_GL_PROGRAM_SPLIT]);
    tt__LoadUpdaterProgramReduction(&programs[TT__UPDATER_GL_PROGRAM_REDUCTION]);
    tt__LoadUpdaterProgramReductionPrepass(&programs[TT__UPDATER_GL_PROGRAM_REDUCTION_PREPASS]);

    for (int i = 0; i < programCount && areProgramsReady == GL_TRUE; ++i) {
        glGetProgramiv(programs[i], GL_LINK_STATUS, &areProgramsReady);

        if (areProgramsReady == GL_FALSE) {
            GLint logSize = 0;
            GLchar *log = NULL;

            glGetProgramiv(programs[i], GL_INFO_LOG_LENGTH, &logSize);
            log = (GLchar *)TT_MALLOC(logSize);
            glGetProgramInfoLog(programs[i], logSize, NULL, log);
            TT_LOG("tt_Texture: GLSL linker failed for program %i\n"
                   "-- Begin -- GLSL Linker Info Log\n%s\n"
                   "-- End -- GLSL Linker Info Log\n", i, log);
            TT_FREE(log);
        }
    }

    if (glGetError() != GL_NO_ERROR || areProgramsReady == GL_FALSE) {
        TT_FREE(programs);

        return false;
    }

    tt->updater.gl.programs = programs;

    return true;
}

static void tt__ReleaseUpdaterPrograms(tt_Texture *tt)
{
    for (int i = 0; i < TT__UPDATER_GL_PROGRAM_COUNT; ++i)
        if (glIsProgram(tt->updater.gl.programs[i]))
            glDeleteProgram(tt->updater.gl.programs[i]);

    TT_FREE(tt->updater.gl.programs);
}


/*******************************************************************************
 * LoadUpdater -- Loads memory for updating the tt_Texture cache
 *
 */
static bool tt__LoadUpdater(tt_Texture *tt)
{
    if (!tt__LoadUpdaterBuffers(tt)) {
        return false;
    }

    if (!tt__LoadUpdaterQueries(tt)) {
        tt__ReleaseUpdaterBuffers(tt);

        return false;
    }

    if (!tt__LoadUpdaterPrograms(tt)) {
        tt__ReleaseUpdaterBuffers(tt);
        tt__ReleaseUpdaterQueries(tt);

        return false;
    }

    tt->updater.streamByteOffset = 0;
    tt->updater.splitOrMerge = 0;
    tt->updater.isReady = GL_TRUE;

    return true;
}


/*******************************************************************************
 * ReleaseUpdater -- Releases memory for updating the tt_Texture cache
 *
 */
static void tt__ReleaseUpdater(tt_Texture *tt)
{
    tt__ReleaseUpdaterBuffers(tt);
    tt__ReleaseUpdaterQueries(tt);
    tt__ReleaseUpdaterPrograms(tt);
}


/*******************************************************************************
 * Load -- Loads a tt_Texture from a file
 *
 * The cacheCapcity parameter tells how many pages the cache
 * stores in memory. The number of GPU textures created is
 * proportional to this parameter, with a factor depending on the page
 * format.
 *
 */
TTDEF tt_Texture *tt_Load(const char *filename, int cacheCapacity)
{
    FILE *stream = fopen(filename, "rb+");
    tt__Header header;
    tt_Texture *tt;

    if (!stream) {
        TT_LOG("tt_Texture: fopen failed");

        return NULL;
    }

    if (!tt__ReadHeader(stream, &header)) {
        TT_LOG("tt_Texture: unsupported file");
        fclose(stream);

        return NULL;
    }

    tt = (tt_Texture *)TT_MALLOC(sizeof(*tt));
    tt__LoadStorage(tt, header, stream);

    if (!tt__LoadCache(tt, cacheCapacity)) {
        tt__ReleaseStorage(tt);
        TT_FREE(tt);

        return NULL;
    }

    if (!tt__LoadUpdater(tt)) {
        tt__ReleaseCache(tt);
        tt__ReleaseStorage(tt);
        TT_FREE(tt);

        return NULL;
    }

    return tt;
}


/*******************************************************************************
 * Release -- Releases a tt_Texture from from memory
 *
 */
TTDEF void tt_Release(tt_Texture *tt)
{
    tt__ReleaseUpdater(tt);
    tt__ReleaseCache(tt);
    tt__ReleaseStorage(tt);
    TT_FREE(tt);
}

/*******************************************************************************
 * StreamModelViewMatrix -- Streams the model-view matrix
 *
 * This procedure simply copies the matrix.
 *
 */
static void
tt__StreamModelViewMatrix(tt__UpdateParameters *p, const tt_UpdateArgs *args)
{
    memcpy(p->modelView, args->matrices.modelView, sizeof(p->modelView));
}


/*******************************************************************************
 * StreamFrustumPlanes -- Streams the frustum planes of the camera
 *
 * The planes are extracted from the model view projection matrix.
 * Based on "Fast Extraction of Viewing Frustum Planes from the World-
 * View-Projection Matrix", by Gil Gribb and Klaus Hartmann.
 *
 */
static void
tt__StreamFrustumPlanes(
    tt__UpdateParameters *parameters,
    const tt_UpdateArgs *args
) {
#define MVP args->matrices.modelViewProjection
#define PLANES parameters->frustumPlanes
    for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 2; ++j) {
        float x = MVP[3 + 4 * 0] + (j == 0 ? MVP[i + 4 * 0] : -MVP[i + 4 * 0]);
        float y = MVP[3 + 4 * 1] + (j == 0 ? MVP[i + 4 * 1] : -MVP[i + 4 * 1]);
        float z = MVP[3 + 4 * 2] + (j == 0 ? MVP[i + 4 * 2] : -MVP[i + 4 * 2]);
        float w = MVP[3 + 4 * 3] + (j == 0 ? MVP[i + 4 * 3] : -MVP[i + 4 * 3]);
        float nrm = sqrtf(x * x + y * y + z * z);

        PLANES[i * 2 + j].x = x * nrm;
        PLANES[i * 2 + j].y = y * nrm;
        PLANES[i * 2 + j].z = z * nrm;
        PLANES[i * 2 + j].w = w * nrm;
    }
#undef PLANES
#undef MVP
}


/*******************************************************************************
 * StreamLodFactor -- Streams the LoD factor
 *
 * This procedure computes the lod factor used to split and/or merge the
 * LEB of the cache. The lod factor computes a target edge size in model view
 * space.
 *
 */
static void
tt__StreamLodFactor(
    tt__UpdateParameters *p,
    const tt_Texture *tt,
    const tt_UpdateArgs *args
) {
    float framebufferHeight = (float)args->framebuffer.height;
    float pageResolution    = (float)(1 << tt->storage.pages.size);
    float virtualResolution = framebufferHeight / pageResolution;
    float pixelsPerTexelTarget = args->pixelsPerTexelTarget;
    float nearPlaneHeight = (float)args->worldSpaceImagePlaneAtUnitDepth.height;
    float targetLength = nearPlaneHeight * (pixelsPerTexelTarget / virtualResolution);
    float isPerspective = (float)args->projection;

    p->lodFactor[0] = 2.0f * (isPerspective - log2f(targetLength));
    p->lodFactor[1] = isPerspective;
}


/*******************************************************************************
 * StreamParameters -- Streams the parameters for updating the cached LEB
 *
 */
static void tt__StreamParameters(tt_Texture *tt, const tt_UpdateArgs *args)
{
    const GLuint *buffers = tt->updater.gl.buffers;
    uint32_t streamByteOffset = tt->updater.streamByteOffset;
    uint32_t streamByteSize = sizeof(tt__UpdateParameters);
    tt__UpdateParameters *parameters;

    if (streamByteOffset + streamByteSize > TT__UPDATER_STREAM_BUFFER_BYTE_SIZE) {
        TT_LOG("tt_Texture: orphaned stream buffer");

        streamByteOffset = 0;
    }

    parameters = (tt__UpdateParameters *)glMapNamedBufferRange(
        buffers[TT__UPDATER_GL_BUFFER_STREAM],
        streamByteOffset, streamByteSize,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );

    tt__StreamModelViewMatrix(parameters, args);
    tt__StreamFrustumPlanes(parameters, args);
    tt__StreamLodFactor(parameters, tt, args);

    glUnmapNamedBuffer(buffers[TT__UPDATER_GL_BUFFER_STREAM]);
    glCopyNamedBufferSubData(
        buffers[TT__UPDATER_GL_BUFFER_STREAM],
        buffers[TT__UPDATER_GL_BUFFER_PARAMETERS],
        streamByteOffset, 0,
        sizeof(tt__UpdateParameters)
    );

    tt->updater.streamByteOffset = streamByteOffset + streamByteSize;
}


/*******************************************************************************
 * RunSplitMergeKernel -- Runs a split/merge kernel
 *
 * This kernel is responsible for updating the LEB heap on the GPU. It either
 * splits or merges nodes depending on their estimated level of detail.
 *
 */
static void tt__RunSplitMergeKernel(tt_Texture *tt, const tt_UpdateArgs *args)
{
    const GLuint *programs = tt->updater.gl.programs;
    const GLuint *buffers = tt->updater.gl.buffers;
    int programID = TT__UPDATER_GL_PROGRAM_MERGE + tt->updater.splitOrMerge;

    tt__StreamParameters(tt, args);
    glBindBufferBase(GL_UNIFORM_BUFFER,
                     TT__UPDATER_GL_BUFFER_PARAMETERS,
                     buffers[TT__UPDATER_GL_BUFFER_PARAMETERS]);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_DISPATCH]);
    glUseProgram(programs[programID]);
    glDispatchComputeIndirect(0);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER,
                     TT__UPDATER_GL_BUFFER_PARAMETERS,
                     0);

    tt->updater.splitOrMerge = 1 - tt->updater.splitOrMerge;
}


/*******************************************************************************
 * RunSumReductionKernel -- Runs a reduction kernel
 *
 * This kernel is responsible for computing a reduction of the LEB heap
 * on the GPU.
 *
 */
static void tt__RunSumReductionKernel(tt_Texture *tt)
{
    const GLuint *programs = tt->updater.gl.programs;
    int it = tt->storage.depth;

    glUseProgram(programs[TT__UPDATER_GL_PROGRAM_REDUCTION_PREPASS]);
    if (true) {
        int cnt = (1 << it) >> 5;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
        int loc = glGetUniformLocation(programs[TT__UPDATER_GL_PROGRAM_REDUCTION_PREPASS],
                                       "u_PassID");

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        it-= 5;
    }

    glUseProgram(programs[TT__UPDATER_GL_PROGRAM_REDUCTION]);
    while (--it >= 0) {
        int loc = glGetUniformLocation(programs[TT__UPDATER_GL_PROGRAM_REDUCTION], "u_PassID");
        int cnt = 1 << it;
        int numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

        glUniform1i(loc, it);
        glDispatchCompute(numGroup, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
}


/*******************************************************************************
 * RunDispatchingKernel -- Runs a dispatchin kernel
 *
 * This kernel is responsible for computing a reduction of the LEB heap
 * on the GPU.
 *
 */
static void tt__RunDispatchingKernel(tt_Texture *tt)
{
    const GLuint *buffers = tt->updater.gl.buffers;
    const GLuint *programs = tt->updater.gl.programs;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_DISPATCH,
                     buffers[TT__UPDATER_GL_BUFFER_DISPATCH]);
    glUseProgram(programs[TT__UPDATER_GL_PROGRAM_DISPATCH]);
    glDispatchCompute(1, 1, 1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_DISPATCH,
                     0);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}


/*******************************************************************************
 * UpdateLeb -- Updates the LEB heap on the GPU
 *
 * This procedure invokes the GPU kernels necessary to update a LEB heap on
 * the GPU.
 *
 */
static void tt__UpdateLeb(tt_Texture *tt, const tt_UpdateArgs *args)
{
    const GLuint *buffers = tt->updater.gl.buffers;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_LEB_GPU,
                     buffers[TT__UPDATER_GL_BUFFER_LEB_GPU]);

    tt__RunSplitMergeKernel(tt, args);
    tt__RunSumReductionKernel(tt);
    tt__RunDispatchingKernel(tt);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_LEB_GPU,
                     0);
}


static bool tt__LebAsynchronousReadBack(tt_Texture *tt)
{
    const GLuint *buffers = tt->updater.gl.buffers;

    if (tt->updater.isReady == GL_TRUE) {
        glCopyNamedBufferSubData(
            buffers[TT__UPDATER_GL_BUFFER_LEB_GPU],
            buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
            0, 0, 2 * sizeof(int32_t) + leb_HeapByteSize(tt->cache.leb)
        );
        glQueryCounter(tt->updater.gl.queries[TT__UPDATER_GL_QUERY_TIMESTAMP],
                       GL_TIMESTAMP);
        tt->updater.isReady = GL_FALSE;
    }

    glGetQueryObjectiv(tt->updater.gl.queries[TT__UPDATER_GL_QUERY_TIMESTAMP],
                       GL_QUERY_RESULT_AVAILABLE,
                       &tt->updater.isReady);

    if (tt->updater.isReady == GL_TRUE) {
        const char *bufferData = (const char *)glMapNamedBufferRange(
            buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
            2 * sizeof(int32_t), leb_HeapByteSize(tt->cache.leb),
            GL_MAP_READ_BIT/* | GL_MAP_UNSYNCHRONIZED_BIT */
        );

        leb_SetHeapMemory(tt->cache.leb, bufferData);
        glUnmapNamedBuffer(buffers[TT__UPDATER_GL_BUFFER_LEB_CPU]);

        return true;
    }

    return false;
}

static void tt__ProducePage(tt_Texture *tt, const tt__Page *page)
{
    // TODO: finalize !
    TT_LOG("Producing page %i using texture %i",
           page->key,
           page->textureID);

    const GLuint *buffers = tt->updater.gl.buffers;
    uint32_t streamByteOffset = tt->updater.streamByteOffset;
    uint32_t streamByteSize = 4 << (2 * tt->storage.pages.size);
    uint8_t *data;

    if (streamByteOffset + streamByteSize > TT__UPDATER_STREAM_BUFFER_BYTE_SIZE) {
        TT_LOG("tt_Texture: orphaned stream buffer");

        streamByteOffset = 0;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers[TT__UPDATER_GL_BUFFER_STREAM]);
    data = (uint8_t *)glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER,
        streamByteOffset, streamByteSize,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );

    srand(page->key);
    uint8_t r = rand() & 255u, g = rand() & 255u, b = rand() & 255u;
    for (int i = 0; i < 1 << (2 * tt->storage.pages.size); ++i) {
        data[4 * i    ] = r;
        data[4 * i + 1] = g;
        data[4 * i + 2] = b;
        data[4 * i + 3] = 255u;
    }

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTextureSubImage3D(tt->cache.gl.textures[0],
                        0,
                        0, 0, page->textureID,
                        1 << tt->storage.pages.size,
                        1 << tt->storage.pages.size,
                        1,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        TT__BUFFER_OFFSET(streamByteOffset));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    tt->updater.streamByteOffset = streamByteOffset + streamByteSize;
}

static tt__Page *tt__LoadPageFromStorage(tt_Texture *tt, uint32_t key)
{
    tt__Page *page = (tt__Page *)malloc(sizeof(*page));
    int cacheSize = HASH_COUNT(tt->cache.pages);

    page->key = key;

    if (cacheSize < tt->cache.capacity) {
        page->textureID = cacheSize;
    } else {
        tt__Page *lruPage, *tmp;

        HASH_ITER(hh, tt->cache.pages, lruPage, tmp) {
            HASH_DELETE(hh, tt->cache.pages, lruPage);
            page->textureID = lruPage->textureID;
            free(lruPage);

            break;
        }
    }

    tt__ProducePage(tt, page);

    return page;
}

static tt__Page *tt__LoadPage(tt_Texture *tt, uint32_t key)
{
    tt__Page *page;

    HASH_FIND(hh, tt->cache.pages, &key, sizeof(key), page);

    if (page) {
        HASH_DELETE(hh, tt->cache.pages, page);
    } else {
        page = tt__LoadPageFromStorage(tt, key);
    }

    HASH_ADD(hh, tt->cache.pages, key, sizeof(key), page);

    return page;
}

static void tt__UpdateCacheMapBuffer(tt_Texture *tt)
{
    const GLuint *buffers = tt->updater.gl.buffers;
    int32_t *map = (int32_t *)glMapNamedBufferRange(
        buffers[TT__UPDATER_GL_BUFFER_MAP],
        0, sizeof(GLint) * tt->cache.capacity,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );

    for (int i = 0; i < leb_NodeCount(tt->cache.leb); ++i) {
        leb_Node node = leb_DecodeNode(tt->cache.leb, i);
        const tt__Page *page = tt__LoadPage(tt, node.id);

        map[i] = page->textureID;
    }

    glUnmapNamedBuffer(buffers[TT__UPDATER_GL_BUFFER_MAP]);
    glCopyNamedBufferSubData(
        tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
        tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_LEB],
        0, 0, 2 * sizeof(int32_t) + leb_HeapByteSize(tt->cache.leb)
    );
    glCopyNamedBufferSubData(
        tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_MAP],
        tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_MAP],
        0, 0, sizeof(GLint) * tt->cache.capacity
    );
}

TTDEF void tt_Update(tt_Texture *tt, const tt_UpdateArgs *args)
{
    tt__UpdateLeb(tt, args);

    if (tt__LebAsynchronousReadBack(tt)) {
        tt__UpdateCacheMapBuffer(tt);
    }
}

TTDEF GLuint tt_LebBuffer(const tt_Texture *tt)
{
    return tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_LEB];
}

TTDEF GLuint tt_IndirectionBuffer(const tt_Texture *tt)
{
    return tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_MAP];
}

TTDEF void tt_BindPageTextures(const tt_Texture *tt, GLenum *textureUnits)
{
    for (int i = 0; i < tt_TexturesPerPage(tt); ++i) {
        glActiveTexture(textureUnits[i]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tt->cache.gl.textures[i]);
    }
}

#undef TT__BUFFER_SIZE
#undef TT__BUFFER_OFFSET

