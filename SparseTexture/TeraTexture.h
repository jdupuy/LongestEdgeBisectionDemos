
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
    TT_FORMAT_PBR   // displacement, normals, albedo
};

// data type
typedef struct tt_Texture tt_Texture;

// ctor / dtor
TTDEF tt_Texture *tt_Load(const char *filename, int cacheByteSize);
TTDEF void tt_Release(tt_Texture *tt);

// creates a file
TTDEF bool tt_Create(const char *file, tt_Format format, int size, int pageSize);

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


/*******************************************************************************
 * Texture Mapping Data Structure
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
} tt__TextureMapper;


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
        tt__TextureMapper *map;     // LRU map
        struct {
            GLuint      *textures;  // texture names
            GLuint64    *handles;   // texture handles
            GLuint      *buffers;   // handles (UBO) + LEB Heap (SSBO)
        } gl;
        int capacity;               // capacity in number of textures
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
        } pixelStream;
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
    FILE *pf = fopen(file, "wb");

    if (!pf) {
        TT_LOG("tt_Texture: fopen failed");
        return false;
    }

    if (fwrite(&header, sizeof(header), 1, pf) != 1) {
        TT_LOG("tt_Texture: header dump failed");
        fclose(pf);
        return false;
    }

    memset(pageData, 0, bytesPerPage);
    for (int i = 0; i < pageCount; ++i) {
        if (fwrite(pageData, bytesPerPage, 1, pf) != 1) {
            TT_LOG("tt_Texture: page dump failed");
            fclose(pf);
            return false;
        }
    }
    TT_FREE(pageData);

    fclose(pf);

    TT_LOG("tt_Texture: wrote %lu Bytes to disk", sizeof(header) + pageCount * bytesPerPage);

    return true;
}


/*******************************************************************************
 * Load -- Load a TeraTexture
 *
 */
static bool tt__ReadHeader(FILE *stream, tt__Header *header)
{
    fread(header, sizeof(*header), 1, stream);

    return header->magic == tt__Magic();
}


static void
tt__LoadStorage(tt_Texture *tt, const tt__Header header, FILE *stream)
{
    tt->storage.stream = stream;
    tt->storage.pages.size = header.pageSize;
    tt->storage.pages.format = (tt_Format)header.format;
    tt->storage.depth = header.depth;
}


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


static bool tt__LoadCacheTextures(tt_Texture *tt)
{
    int textureSize = 1 << tt->storage.pages.size;
    int texturesPerPage = tt__TexturesPerPage(tt);
    int textureCount = tt->cache.capacity * texturesPerPage;
    GLuint *textures = (GLuint *)TT_MALLOC(sizeof(GLuint) * textureCount);
    GLuint64 *handles = (GLuint64 *)TT_MALLOC(sizeof(GLuint64) * textureCount);

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


enum {
    TT__CACHE_GL_BUFFER_HANDLES,
    TT__CACHE_GL_BUFFER_LEB,
    TT__CACHE_GL_BUFFER_COUNT,
};


static bool tt__LoadCacheBuffers(tt_Texture *tt)
{
    GLuint *buffers = (GLuint *)TT_MALLOC(sizeof(GLuint) * TT__CACHE_GL_BUFFER_COUNT);

    glGenBuffers(TT__CACHE_GL_BUFFER_COUNT, buffers);

    glBindBuffer(GL_UNIFORM_BUFFER, buffers[TT__CACHE_GL_BUFFER_HANDLES]);
    glBufferStorage(GL_UNIFORM_BUFFER,
                    sizeof(GLuint64) * tt->cache.capacity,
                    NULL,
                    GL_MAP_WRITE_BIT);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                 buffers[TT__CACHE_GL_BUFFER_HANDLES]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    sizeof(uint32_t) * (1u << (tt->storage.depth - 1)),
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}


static void tt__ReleaseCacheBuffers(tt_Texture *tt)
{
    glDeleteBuffers(TT__CACHE_GL_BUFFER_COUNT, tt->cache.gl.buffers);
    TT_FREE(tt->cache.gl.buffers);
}


static void tt__ReleaseCache(tt_Texture *tt)
{
    tt__ReleaseCacheBuffers(tt);
    tt__ReleaseCacheTextures(tt);
}


static bool tt__LoadCache(tt_Texture *tt, int cacheByteSize)
{
    size_t bytesPerPage = tt__BytesPerPage(tt->storage.pages.format,
                                           tt->storage.pages.size);
    tt->cache.map = NULL;
    tt->cache.capacity = cacheByteSize / bytesPerPage;

    if (!tt__LoadCacheTextures(tt)) {
        return false;
    }

    if (!tt__LoadCacheBuffers(tt)) {
        tt__ReleaseCacheTextures(tt);

        return false;
    }

    return true;
}

static bool tt__LoadUpdater(tt_Texture *tt)
{
    return true;
}


TTDEF tt_Texture *tt_Load(const char *filename, int cacheByteSize)
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

    if (!tt__LoadCache(tt, cacheByteSize)) {
        tt__ReleaseStorage(tt);
        TT_FREE(tt);

        return NULL;
    }

    if (!tt__LoadUpdater(tt)) {
        tt__ReleaseCache(tt);
        tt__ReleaseStream(tt);
        TT_FREE(tt);

        return NULL;
    }

    return tt;
}

static void tt__ReleaseUpdater(tt_Texture *tt)
{

}

static void tt__ReleaseStorage(tt_Texture *tt)
{

}


TTDEF void tt_Release(tt_Texture *tt)
{
    tt__ReleaseUpdater(tt);
    tt__ReleaseCache(tt);
    tt__ReleaseStorage(tt);
    TT_FREE(tt);
}

