
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

// update arguments
typedef struct {
    struct {
        float modelView[16];                // col-major modelView matrix
        float modelViewProjection[16];      // col-major modelViewProjection matrix
    } matrices;

    float framebufferResolution;            //
    float imagePlaneSlope;
    float pixelToTexelTarget;
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

#ifdef __cplusplus
} // extern "C"
#endif

//
//
//// end header file ///////////////////////////////////////////////////////////
#endif // TT_INCLUDE_TT_H


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

#include "uthash.h"
#include "LongestEdgeBisection.h"


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
            GLuint64    *handles;   // texture handles
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
        struct {
            int capacity;           // capacity in bytes
            int offset;             // current offset pos in Bytes
        } stream;
        GLint isReady;
        GLint splitOrMerge;
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
static int tt__TexturesPerPage(const tt_Texture *tt)
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
    int texturesPerPage = tt__TexturesPerPage(tt);
    int textureCount = tt->cache.capacity * texturesPerPage;
    GLuint *textures = (GLuint *)TT_MALLOC(sizeof(GLuint) * textureCount);
    GLuint64 *handles = (GLuint64 *)TT_MALLOC(sizeof(GLuint64) * textureCount);

    TT_LOG("tt_Texture: allocating %lu MiBytes of GPU memory using %i textures\n",
           (textureCount * tt__BytesPerPage(tt->storage.pages.format, tt->storage.pages.size)) >> 20,
           textureCount);
    glGenTextures(textureCount, textures);
    for (int j = 0; j < texturesPerPage; ++j) {
        for (int i = 0; i < tt->cache.capacity; ++i) {
            GLuint *texture = &textures[i + tt->cache.capacity * j];
            GLuint64 *handle = &handles[i + tt->cache.capacity * j];

            glBindTexture(GL_TEXTURE_2D, *texture);

            glTextureStorage2D(*texture, 1, GL_RGBA8, textureSize, textureSize);
            glTextureParameteri(*texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(*texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(*texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(*texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

            (*handle) = glGetTextureHandleARB(*texture);
            glMakeTextureHandleResidentARB(*handle);
        }
    }

    tt->cache.gl.textures = textures;
    tt->cache.gl.handles = handles;

    return (glGetError() == GL_NO_ERROR);
}


/*******************************************************************************
 * ReleaseCacheTextures -- Cleans up GPU texture memory
 *
 */
static void tt__ReleaseCacheTextures(tt_Texture *tt)
{
    int textureCount = tt->cache.capacity * tt__TexturesPerPage(tt);

    for (int i = 0; i < textureCount; ++i) {
        glMakeTextureHandleNonResidentARB(tt->cache.gl.handles[i]);
    }

    glDeleteTextures(textureCount, tt->cache.gl.textures);
    TT_FREE(tt->cache.gl.textures);
    TT_FREE(tt->cache.gl.handles);
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
                    leb_HeapByteSize(tt->cache.leb),
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, buffers[TT__CACHE_GL_BUFFER_MAP]);
    glBufferStorage(GL_UNIFORM_BUFFER,
                    sizeof(GLuint64) * tt->cache.capacity * tt__TexturesPerPage(tt),
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
    tt->cache.leb = leb_Create(tt->storage.depth);
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

enum {
    TT__UPDATER_GL_BUFFER_DISPATCH,
    TT__UPDATER_GL_BUFFER_LEB_CPU,
    TT__UPDATER_GL_BUFFER_LEB_GPU,
    TT__UPDATER_GL_BUFFER_MAP,
    TT__UPDATER_GL_BUFFER_PARAMETERS,
    TT__UPDATER_GL_BUFFER_PIXEL_UNPACK,

    TT__UPDATER_GL_BUFFER_COUNT
};

static bool tt__LoadUpdaterBuffers(tt_Texture *tt)
{
    int bufferCount = TT__UPDATER_GL_BUFFER_COUNT;
    GLuint *buffers = (GLuint *)TT_MALLOC(sizeof(GLuint) * bufferCount);
    const uint32_t dispatchData[8] = {2, 1, 1, 0, 0, 0, 0, 0};

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
                    leb_HeapByteSize(tt->cache.leb),
                    NULL,
                    GL_MAP_READ_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_LEB_GPU]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    leb_HeapByteSize(tt->cache.leb),
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_COPY_READ_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_MAP]);
    glBufferStorage(GL_COPY_READ_BUFFER,
                    sizeof(GLuint64) * tt->cache.capacity * tt__TexturesPerPage(tt),
                    NULL,
                    GL_MAP_WRITE_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_PARAMETERS]);
    glBufferStorage(GL_UNIFORM_BUFFER,
                    sizeof(dispatchData),
                    dispatchData,
                    GL_MAP_READ_BIT);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBuffer(GL_COPY_READ_BUFFER,
                 buffers[TT__UPDATER_GL_BUFFER_PIXEL_UNPACK]);
    glBufferStorage(GL_COPY_READ_BUFFER,
                    sizeof(dispatchData),
                    dispatchData,
                    GL_MAP_READ_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(TT__UPDATER_GL_BUFFER_COUNT, buffers);
        TT_FREE(buffers);

        return false;
    }

    tt->updater.gl.buffers = buffers;

    return true;
}


enum {
    TT__UPDATER_GL_QUERY_TIMESTAMP,

    TT__UPDATER_GL_QUERY_COUNT
};

enum {
    TT__UPDATER_GL_PROGRAM_SPLIT,
    TT__UPDATER_GL_PROGRAM_MERGE,
    TT__UPDATER_GL_PROGRAM_DISPATCH,
    TT__UPDATER_GL_PROGRAM_REDUCTION,
    TT__UPDATER_GL_PROGRAM_REDUCTION_PREPASS,

    TT__UPDATER_GL_PROGRAM_COUNT
};


static bool tt__LoadUpdaterProgramUpdate(tt_Texture *tt)
{
    //GLuint program = glCreateShaderProgramv(GL_COMPUTE_SHADER, 1, NULL);
    //tt->updater.gl.programs[TT__UPDATER_GL_PROGRAM_UPDATE] = program;

    return (glGetError() == GL_NO_ERROR);
}

static bool tt__LoadUpdaterProgramDispatch(tt_Texture *tt)
{
    //GLuint program = glCreateShaderProgramv(GL_COMPUTE_SHADER, 1, NULL);
    //tt->updater.gl.programs[TT__UPDATER_GL_PROGRAM_UPDATE] = program;

    return (glGetError() == GL_NO_ERROR);
}

static bool tt__LoadUpdaterProgramReduction(tt_Texture *tt)
{
    //GLuint program = glCreateShaderProgramv(GL_COMPUTE_SHADER, 1, NULL);
    //tt->updater.gl.programs[TT__UPDATER_GL_PROGRAM_UPDATE] = program;

    return (glGetError() == GL_NO_ERROR);
}

static bool tt__LoadUpdaterProgramReductionPrepass(tt_Texture *tt)
{

    //GLuint program = glCreateShaderProgramv(GL_COMPUTE_SHADER, 1, NULL);

    //tt->updater.gl.programs[TT__UPDATER_GL_PROGRAM_UPDATE] = program;

    return (glGetError() == GL_NO_ERROR);
}

static bool tt__LoadUpdaterPrograms(tt_Texture *tt)
{
    int tmp = sizeof(GLuint) * TT__UPDATER_GL_PROGRAM_COUNT;

    tt->updater.gl.programs = (GLuint *)TT_MALLOC(tmp);

    tt__LoadUpdaterProgramUpdate(tt);
    tt__LoadUpdaterProgramDispatch(tt);
    tt__LoadUpdaterProgramReduction(tt);
    tt__LoadUpdaterProgramReductionPrepass(tt);

    return true;
}

static bool tt__LoadUpdaterQueries(tt_Texture *tt)
{
    int tmp = sizeof(GLuint) * TT__UPDATER_GL_QUERY_COUNT;

    tt->updater.gl.queries = (GLuint *)TT_MALLOC(tmp);

    glGenQueries(TT__UPDATER_GL_QUERY_COUNT, tt->updater.gl.queries);
    glQueryCounter(tt->updater.gl.queries[TT__UPDATER_GL_QUERY_TIMESTAMP],
                   GL_TIMESTAMP);

    return (glGetError() == GL_NO_ERROR);
}

static void tt__ReleaseUpdaterPrograms(tt_Texture *tt)
{
    for (int i = 0; i < TT__UPDATER_GL_PROGRAM_COUNT; ++i)
        if (glIsProgram(tt->updater.gl.programs[i]))
            glDeleteProgram(tt->updater.gl.programs[i]);

    TT_FREE(tt->updater.gl.programs);
}

static void tt__ReleaseUpdaterBuffers(tt_Texture *tt)
{
    glDeleteBuffers(TT__UPDATER_GL_BUFFER_COUNT, tt->updater.gl.buffers);

    TT_FREE(tt->updater.gl.buffers);
}

static void tt__ReleaseUpdaterQueries(tt_Texture *tt)
{
    glDeleteQueries(TT__UPDATER_GL_QUERY_COUNT, tt->updater.gl.queries);

    TT_FREE(tt->updater.gl.queries);
}

static void tt__ReleaseUpdater(tt_Texture *tt)
{
    tt__ReleaseUpdaterBuffers(tt);
    tt__ReleaseUpdaterQueries(tt);
    tt__ReleaseUpdaterPrograms(tt);
}

static bool tt__LoadUpdater(tt_Texture *tt)
{
    bool v = true;

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

    return true;
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
 * Release -- Releases a tt_Texture from from memory
 *
 */

static void tt__UpdateLeb(tt_Texture *tt, const tt_UpdateArgs args)
{
#if 0
    tt__RunSplitMergeKernel(tt, args);
    tt__RunSumReductionKernel(tt);
    tt__RunIndirectBatchingKernel(tt);
#endif
}

static bool tt__CopyLebAsynchronous(tt_Texture *tt)
{
    if (tt->updater.isReady == GL_TRUE) {
        glCopyNamedBufferSubData(
            tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_LEB_GPU],
            tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
            0, 0, 256/*leb_HeapByteSize(tt->storage.depth)*/
        );
        glQueryCounter(tt->updater.gl.queries[TT__UPDATER_GL_QUERY_TIMESTAMP],
                       GL_TIMESTAMP);
        tt->updater.isReady = GL_FALSE;
    }

    glGetQueryObjectiv(tt->updater.gl.queries[TT__UPDATER_GL_QUERY_TIMESTAMP],
                       GL_QUERY_RESULT_AVAILABLE,
                       &tt->updater.isReady);

    return tt->updater.isReady == GL_TRUE;
}

static void tt__ProducePage(tt_Texture *tt, const tt__Page *page)
{
    TT_LOG("Producing page %i using texture %i (%i)\n",
           page->key,
           page->textureID,
           tt->cache.gl.textures[page->textureID]);
}

static tt__Page *tt__LoadPageFromStorage(tt_Texture *tt, uint32_t key)
{
    tt__Page *page = (tt__Page *)malloc(sizeof(*page));
    int cacheSize = HASH_COUNT(tt->cache.pages);

    TT_LOG("tt_Texture: Cache Size: %i\n", cacheSize);

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

static void tt__UpdateCache(tt_Texture *tt)
{
    leb_Heap *leb; // TODO: init
    GLuint64 *handles;

#if 0
    (GLuint64 *)glMapNamedBufferRange(
        tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_STREAM_HANDLES],
        0, /*leb_HeapByteSize(tt->storage.depth)*/256,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );
    // TODO: stop at buffer capacity and take page format into account
    for (int i = 0; i < leb_NodeCount(leb); ++i) {
        leb_Node node = leb_DecodeNode(leb, i);
        const tt__Page *page = tt__LoadPage(tt, node.id);

        handles[i] = tt->cache.gl.textures[map->textureID].handle;
    }
#endif

    glUnmapNamedBuffer(tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_MAP]);

    glCopyNamedBufferSubData(
        tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
        tt->updater.gl.buffers[TT__CACHE_GL_BUFFER_LEB],
        0, 0, 256/*leb_HeapByteSize(tt->storage.depth)*/
    );
    glCopyNamedBufferSubData(
        tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_MAP],
        tt->updater.gl.buffers[TT__CACHE_GL_BUFFER_MAP],
        0, 0, 256/*leb_HeapByteSize(tt->storage.depth)*/
    );
}

TTDEF void tt_Update(tt_Texture *tt, const tt_UpdateArgs args)
{
    tt__UpdateLeb(tt, args);

    if (tt__CopyLebAsynchronous(tt)) {
        tt__UpdateCache(tt);
    }
}

