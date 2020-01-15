
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

// supported page texture formats
typedef enum {
    // 8-bit
    TT_FORMAT_R8,
    TT_FORMAT_RG8,
    TT_FORMAT_RGBA8,
    // 16-bit
    TT_FORMAT_R16,
    TT_FORMAT_RG16,
    TT_FORMAT_RGBA16,
    // half
    TT_FORMAT_R16F,
    TT_FORMAT_RG16F,
    TT_FORMAT_RGBA16F,
    // float
    TT_FORMAT_R32F,
    TT_FORMAT_RG32F,
    TT_FORMAT_RGBA32F,
    // BCn
    TT_FORMAT_BC1,
    TT_FORMAT_BC1_ALPHA,
    TT_FORMAT_BC2,
    TT_FORMAT_BC3,
    TT_FORMAT_BC4,
    TT_FORMAT_BC5,
    TT_FORMAT_BC6,
    TT_FORMAT_BC6_SIGNED,
    TT_FORMAT_BC7,
    TT_FORMAT_BC7_SRGB
} tt_Format;

// data type
typedef struct tt_Texture tt_Texture;

// create a file
TTDEF bool tt_Create(const char *file,
                     int64_t textureSize,
                     int64_t pageSize,
                     tt_Format pageFormat);
TTDEF bool tt_CreateLayered(const char *file,
                            int64_t textureSize,
                            int64_t texturesPerPage,
                            const int64_t *pageTextureSizes,
                            const tt_Format *pageTextureFormats);

// ctor / dtor
TTDEF tt_Texture *tt_Load(const char *filename, int64_t cacheCapacity);
TTDEF void tt_Release(tt_Texture *tt);

// queries
TTDEF int64_t tt_MaxTexturesPerPage();
TTDEF int64_t tt_StorageSize(const tt_Texture *tt);
TTDEF int64_t tt_PageCount(const tt_Texture *tt);
TTDEF int64_t tt_TexturesPerPage(const tt_Texture *tt);
TTDEF tt_Format tt_PageTextureFormat(const tt_Texture *tt, int64_t textureID);
TTDEF int64_t tt_PageTextureSize(const tt_Texture *tt, int64_t textureID);
TTDEF int64_t tt_BytesPerPage(const tt_Texture *tt);
TTDEF int64_t tt_BytesPerPageTexture(const tt_Texture *tt, int64_t textureID);

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

// update
TTDEF void tt_Update(tt_Texture *tt, const tt_UpdateArgs *args);

// enable displacement mapping (last layer by convention)
TTDEF void tt_Displace(tt_Texture *tt);

// raw OpenGL accessors
TTDEF GLuint tt_LebBuffer(const tt_Texture *tt);
TTDEF GLuint tt_IndirectionBuffer(const tt_Texture *tt);

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
 * Per-Page Texture Info Data Structure
 *
 */
typedef struct {
    int8_t size;
    int8_t format;
} tt__PageTextureInfo;


/*******************************************************************************
 * Header File Data Structure
 *
 * This represents the header we use to uniquely identify the tt_Texture files
 * and provide the fundamental information to properly decode the rest of the
 * file.
 *
 */
typedef struct {
    int64_t magic;              // integer value
    int32_t depth;              // LEB depth
    int32_t texturesPerPage;    // number of textures per page
    tt__PageTextureInfo textures[/* max number of textures per page: */ 8];
} tt__Header;


/*******************************************************************************
 * Texture Data Structure
 *
 * This is the main data structure of the library. It consists of 3 different
 * modules: the storage, the cache, and the updater. The storage describes the
 * where the entire texture is stored in memory. The cache is responsible for
 * storing the parts of the texture that are stored in GPU memory. Finally,
 * the updater is responsible for updating the cache, by loading necessary
 * pages into memory.
 * Note that the storage is graphics-API agnostic, while the cache and updater
 * rely on OpenGL (porting to different APIs should be straightforward).
 *
 */
struct tt_Texture {
    struct {
        FILE *stream;               // pointer to file
        tt__Header header;          // file header
    } storage;

    struct {
        tt__Page *pages;            // LRU map
        leb_Heap *leb;              // LEB
        struct {
            GLuint *textures;       // texture names
            GLuint *buffers;        // handles (UBO) + LEB Heap (SSBO)
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
 * Magic -- Generates the magic identifier
 *
 * Each tt_Texture file starts with 4 Bytes that allow us to check if the file
 * under reading is actually a tt_Texture file.
 *
 */
static int64_t tt__Magic()
{
    const union {
        char    string[8];
        int64_t numeric;
    } magic = {.string = {'T', 'T', 'e', 'x', 't', 'u', 'r', 'e'}};

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
static int32_t tt__SizeToLebDepth(int32_t textureSize, int32_t pageSize)
{
    return 2 * (textureSize - pageSize) + 1;
}


/*******************************************************************************
 * PageCount -- Computes the number of pages stored in the tt_Texture file
 *
 */
static int64_t tt__PageCount(int32_t lebDepth)
{
    return 2 << lebDepth;
}

TTDEF int64_t tt_PageCount(const tt_Texture *tt)
{
    return tt__PageCount(tt->storage.header.depth);
}


/*******************************************************************************
 * BytesPerPageTexture -- Computes the Byte size of a page texture
 *
 */
static int64_t tt__BytesPerPageTexture(int64_t textureSize, tt_Format format)
{
    int64_t texelCount = 1u << (2 * textureSize);

    switch (format) {
    case TT_FORMAT_BC1:
    case TT_FORMAT_BC1_ALPHA:
    case TT_FORMAT_BC4:
        return texelCount >> 1; // 1/2 Byte per texel

    case TT_FORMAT_R8:
    case TT_FORMAT_BC2:
    case TT_FORMAT_BC3:
    case TT_FORMAT_BC5:
    case TT_FORMAT_BC6:
    case TT_FORMAT_BC6_SIGNED:
    case TT_FORMAT_BC7:
    case TT_FORMAT_BC7_SRGB:
        return texelCount;      // 1 Byte per texel

    case TT_FORMAT_RG8:
    case TT_FORMAT_R16:
    case TT_FORMAT_R16F:
        return texelCount << 1; // 2 Bytes per texel

    case TT_FORMAT_RGBA8:
    case TT_FORMAT_RG16:
    case TT_FORMAT_RG16F:
    case TT_FORMAT_R32F:
        return texelCount << 2; // 4 Bytes per texel

    case TT_FORMAT_RGBA16:
    case TT_FORMAT_RGBA16F:
    case TT_FORMAT_RG32F:
        return texelCount << 3; // 8 Bytes per texel

    case TT_FORMAT_RGBA32F:
        return texelCount << 4; // 16 Bytes per texel
    }
}

TTDEF int64_t tt_BytesPerPageTexture(const tt_Texture *tt, int64_t textureID)
{
    const tt__PageTextureInfo *info = &tt->storage.header.textures[textureID];

    return tt__BytesPerPageTexture((int64_t)info->size, (tt_Format)info->format);
}


/*******************************************************************************
 * BytesPerPage -- Computes the Byte size of a single page stored
 *
 */
static int64_t tt__BytesPerPage(const tt__Header *header)
{
    const tt__PageTextureInfo *textures = header->textures;
    int64_t size = 0;

    for (int64_t i = 0; i < header->texturesPerPage; ++i) {
        size+= tt__BytesPerPageTexture((int64_t)textures[i].size,
                                       (tt_Format)textures[i].format);
    }

    return size;
}

TTDEF int64_t tt_BytesPerPage(const tt_Texture *tt)
{
    return tt__BytesPerPage(&tt->storage.header);
}


/*******************************************************************************
 * PageTextureFormat -- Returns the format of the page texture indexed by textureID
 *
 */
TTDEF tt_Format tt_PageTextureFormat(const tt_Texture *tt, int64_t textureID)
{
    return (tt_Format)tt->storage.header.textures[textureID].format;
}


/*******************************************************************************
 * PageTextureSize -- Returns the size of the page texture indexed by textureID
 *
 */
TTDEF int64_t tt_PageTextureSize(const tt_Texture *tt, int64_t textureID)
{
    return (int64_t)tt->storage.header.textures[textureID].size;
}


/*******************************************************************************
 * StorageSize -- Returns the size of the tt_Texture file
 *
 */
TTDEF int64_t tt_StorageSize(const tt_Texture *tt)
{
    return (sizeof(tt__Header) + tt_BytesPerPage(tt) * tt_PageCount(tt));
}


/*******************************************************************************
 * MaxTexturesPerPage
 *
 * This procedure returns the maximum number of textures per page.
 * Rather than having a macro I prefer to hardcode the number in
 * the tt__Header structure and query it directly here.
 *
 */
TTDEF int64_t tt_MaxTexturesPerPage()
{
    tt__Header head;

    return TT__BUFFER_SIZE(head.textures);
}


/*******************************************************************************
 * Create header
 *
 * This procedure initializes a tt_Texture file header given an input
 * texture size, and a description of pages.
 *
 */
static tt__Header
tt__CreateHeader(
    int64_t textureSize,
    int64_t texturesPerPage,
    const int64_t* pageTextureSizes,
    const tt_Format* pageTextureFormats
) {
    TT_ASSERT(texturesPerPage < tt_MaxTexturesPerPage() && "texturesPerPage should be less than tt_MaxTexturesPerPage()");
    TT_ASSERT(textureSize > pageTextureSizes[0] && "the page size should be less than the size of the texture");

    tt__Header header = {
        tt__Magic(),
        tt__SizeToLebDepth(textureSize, pageTextureSizes[0]),
        (int32_t)texturesPerPage,
        {{0, 0}}
    };

    for (int64_t i = 0; i < texturesPerPage; ++i) {
        header.textures[i].size     = (int8_t)pageTextureSizes[i];
        header.textures[i].format   = (int8_t)pageTextureFormats[i];
    }

    return header;
}


/*******************************************************************************
 * Create -- Allocates storage for a tt_Texture file
 *
 * This procedure creates a file on disk that stores the data. The function
 * returns true if the file is successfully created, and false otherwise.
 * The size describes the texel resolution of the texture in base 2 logarithm.
 * The user is required to provide the number of textures per page, and
 * an array of PageTextureInfo that provides information about each texture
 * stored within a page.
 * Note that we compute the LEB depth depending on the size of the Tera Texture,
 * and the size of the first texture in the page list.
 *
 */
TTDEF bool
tt_CreateLayered(
    const char *file,
    int64_t textureSize,
    int64_t texturesPerPage,
    const int64_t *pageTextureSizes,
    const tt_Format *pageTextureFormats
) {
    tt__Header header = tt__CreateHeader(textureSize,
                                         texturesPerPage,
                                         pageTextureSizes,
                                         pageTextureFormats);
    int64_t bytesPerPage = tt__BytesPerPage(&header);
    uint8_t *pageData = (uint8_t *)TT_MALLOC(bytesPerPage);
    FILE *stream = fopen(file, "wb");

    if (!stream) {
        TT_LOG("tt_Texture: fopen failed");

        return false;
    }

    if (header.depth >= 28) {
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
    for (int64_t i = 0; i < tt__PageCount(header.depth); ++i) {
        if (fwrite(pageData, bytesPerPage, 1, stream) != 1) {
            TT_LOG("tt_Texture: page dump failed");
            fclose(stream);

            return false;
        }
    }
    TT_FREE(pageData);

    fclose(stream);

    TT_LOG("tt_Texture: file creation successful");

    return true;
}

TTDEF bool
tt_Create(
    const char *file,
    int64_t textureSize,
    int64_t pageSize,
    tt_Format pageFormat
) {
    return tt_CreateLayered(file, textureSize, 1, &pageSize, &pageFormat);
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
tt__LoadStorage(tt_Texture *tt, const tt__Header *header, FILE *stream)
{
    tt->storage.stream = stream;
    tt->storage.header = *header;
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
TTDEF int64_t tt_TexturesPerPage(const tt_Texture *tt)
{
    return tt->storage.header.texturesPerPage;
}


/*******************************************************************************
 * PageTextureInternalFormat -- Provides the internal format of a page texture
 *
 */
static GLint tt__PageTextureInternalFormat(const tt_Texture *tt, int textureID)
{
    const tt__PageTextureInfo *info = &tt->storage.header.textures[textureID];

    switch (info->format) {
    case TT_FORMAT_R8:          return GL_R8;
    case TT_FORMAT_RG8:         return GL_RG8;
    case TT_FORMAT_RGBA8:       return GL_RGBA8;
    case TT_FORMAT_R16:         return GL_R16;
    case TT_FORMAT_RG16:        return GL_RG16;
    case TT_FORMAT_RGBA16:      return GL_RGBA16;
    case TT_FORMAT_R16F:        return GL_R16F;
    case TT_FORMAT_RG16F:       return GL_RG16F;
    case TT_FORMAT_RGBA16F:     return GL_RGBA16F;
    case TT_FORMAT_R32F:        return GL_R32F;
    case TT_FORMAT_RG32F:       return GL_RG32F;
    case TT_FORMAT_RGBA32F:     return GL_RGBA32F;
    case TT_FORMAT_BC1:         return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case TT_FORMAT_BC1_ALPHA:   return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case TT_FORMAT_BC2:         return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    case TT_FORMAT_BC3:         return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    case TT_FORMAT_BC4:         return GL_COMPRESSED_RED_RGTC1_EXT;
    case TT_FORMAT_BC5:         return GL_COMPRESSED_RED_GREEN_RGTC2_EXT;
    case TT_FORMAT_BC6:         return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
    case TT_FORMAT_BC6_SIGNED:  return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
    case TT_FORMAT_BC7:         return GL_COMPRESSED_RGBA_BPTC_UNORM;
    case TT_FORMAT_BC7_SRGB:    return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
    }

    return 0;
}

/*******************************************************************************
 * LoadCacheTextures -- Allocates GPU texture memory for the cache
 *
 */
static bool tt__LoadCacheTextures(tt_Texture *tt)
{
    int64_t texturesPerPage = tt_TexturesPerPage(tt);
    GLuint *textures = (GLuint *)TT_MALLOC(sizeof(GLuint) * texturesPerPage);

    TT_LOG("tt_Texture: allocating %lu MiBytes of GPU memory using %li texture(s)",
           (tt->cache.capacity * texturesPerPage * tt_BytesPerPage(tt)) >> 20,
           texturesPerPage);
    glGenTextures(texturesPerPage, textures);

    for (int64_t i = 0; i < texturesPerPage; ++i) {
        GLint textureSize = 1 << (tt->storage.header.textures[i].size);
        GLuint *texture = &textures[i];
        GLint internalFormat = tt__PageTextureInternalFormat(tt, i);

        glBindTexture(GL_TEXTURE_2D_ARRAY, *texture);

        glTextureStorage3D(*texture, 1, internalFormat,
                           textureSize, textureSize, tt->cache.capacity);
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
    TT__CACHE_GL_BUFFER_LEB,            // LEB heap
    TT__CACHE_GL_BUFFER_INDIRECTION,    // mapping from nodeID to textureID

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
                    leb_HeapByteSize(tt->cache.leb) + 2 * sizeof(int32_t),
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[TT__CACHE_GL_BUFFER_INDIRECTION]);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER,
                    sizeof(GLint) * tt->cache.capacity,
                    NULL,
                    0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

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
    tt->cache.leb = leb_CreateMinMax(1, tt->storage.header.depth);
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
    TT__UPDATER_GL_BUFFER_INDIRECTION,
    TT__UPDATER_GL_BUFFER_PARAMETERS,
    TT__UPDATER_GL_BUFFER_STREAM,

    TT__UPDATER_GL_BUFFER_COUNT
};


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
                 buffers[TT__UPDATER_GL_BUFFER_INDIRECTION]);
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

static const char *tt__TeraTextureShaderSource()
{
    static const char *str =
        #include "TeraTexture.glsl.str"
    ;

    return str;
}

static void
tt__LoadUpdaterProgramSplitMerge(
    GLuint *program,
    const char *flag,
    int64_t texturesPerPage,
    bool displace
) {
    char header[256];
    const char *displacementFlag = displace ? "#define FLAG_DISPLACE 1\n" : "";
    const char *strings[] = {
        "#version 450\n",
        header,
        flag,
        displacementFlag,
        tt__LongestEdgeBisectionLibraryShaderSource(),
        tt__TeraTextureShaderSource(),
        tt__LongestEdgeBisectionUpdateShaderSource()
    };

    sprintf(header,
            "#define LEB_BUFFER_COUNT 2\n"
            "#define TT_LEB_ID 1\n"
            "#define BUFFER_BINDING_LEB %i\n"
            "#define BUFFER_BINDING_PARAMETERS %i\n"
            "#define TT_TEXTURES_PER_PAGE %li\n"
            "#define TT_BUFFER_BINDING_INDIRECTION %i\n",
            TT__UPDATER_GL_BUFFER_LEB_GPU,
            TT__UPDATER_GL_BUFFER_PARAMETERS,
            texturesPerPage,
            TT__CACHE_GL_BUFFER_INDIRECTION);

    *program = glCreateShaderProgramv(GL_COMPUTE_SHADER,
                                      TT__BUFFER_SIZE(strings),
                                      strings);
}

static void
tt__LoadUpdaterProgramSplit(
    GLuint *program,
    int64_t texturesPerPage,
    bool displace
) {
    tt__LoadUpdaterProgramSplitMerge(program,
                                     "#define FLAG_SPLIT 1\n",
                                     texturesPerPage,
                                     displace);
}

static void
tt__LoadUpdaterProgramMerge(
    GLuint *program,
    int64_t texturesPerPage,
    bool displace
) {
    tt__LoadUpdaterProgramSplitMerge(program,
                                     "#define FLAG_MERGE 1\n",
                                     texturesPerPage,
                                     displace);
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
    tt__LoadUpdaterProgramMerge(&programs[TT__UPDATER_GL_PROGRAM_MERGE], tt_TexturesPerPage(tt), false);
    tt__LoadUpdaterProgramSplit(&programs[TT__UPDATER_GL_PROGRAM_SPLIT], tt_TexturesPerPage(tt), false);
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


TTDEF void tt_Displace(tt_Texture *tt)
{
    GLuint *programs = tt->updater.gl.programs;

    tt__LoadUpdaterProgramMerge(&programs[TT__UPDATER_GL_PROGRAM_MERGE],
                                tt_TexturesPerPage(tt),
                                true);
    tt__LoadUpdaterProgramSplit(&programs[TT__UPDATER_GL_PROGRAM_SPLIT],
                                tt_TexturesPerPage(tt),
                                true);
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
    tt__LoadStorage(tt, &header, stream);

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
    float pageResolution    = (float)(1 << tt->storage.header.textures[0].size);
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
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_LEB_GPU + 1,
                     buffers[TT__CACHE_GL_BUFFER_LEB]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__CACHE_GL_BUFFER_INDIRECTION,
                     buffers[TT__CACHE_GL_BUFFER_INDIRECTION]);
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
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__UPDATER_GL_BUFFER_LEB_GPU + 1,
                     0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     TT__CACHE_GL_BUFFER_INDIRECTION,
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
    int it = tt->storage.header.depth;

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
    TT_LOG("tt_Texture: Producing page %i using texture %i",
           page->key,
           page->textureID);

    const GLuint *buffers = tt->updater.gl.buffers;
    int64_t streamByteOffset = (int64_t)tt->updater.streamByteOffset;
    int64_t streamByteSize = tt_BytesPerPage(tt);
    int64_t pageDataOffset = 0;
    uint8_t *pageData;

    if (streamByteOffset + streamByteSize > TT__UPDATER_STREAM_BUFFER_BYTE_SIZE) {
        TT_LOG("tt_Texture: orphaned stream buffer");

        streamByteOffset = 0;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers[TT__UPDATER_GL_BUFFER_STREAM]);
    pageData = (uint8_t *)glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER,
        streamByteOffset, streamByteSize,
        GL_MAP_WRITE_BIT /*| GL_MAP_UNSYNCHRONIZED_BIT*/ // XXX: UNSYNCHRONIZED sometimes produces shitty results
    );

    fseek(tt->storage.stream,
          sizeof(tt__Header) + (uint64_t)page->key * streamByteSize,
          SEEK_SET);
    fread(pageData, streamByteSize, 1, tt->storage.stream);

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    for (int64_t i = 0; i < tt_TexturesPerPage(tt); ++i) {
        GLint textureSize = 1 << tt->storage.header.textures[i].size;
        int64_t textureByteSize = tt_BytesPerPageTexture(tt, i);
        int64_t streamOffset = streamByteOffset + pageDataOffset;
        tt_Format textureFormat = tt_PageTextureFormat(tt, i);

        if (textureFormat >= TT_FORMAT_BC1) {
            glCompressedTextureSubImage3D(tt->cache.gl.textures[i],
                                          0,
                                          0, 0, page->textureID,
                                          textureSize,
                                          textureSize,
                                          1,
                                          tt__PageTextureInternalFormat(tt, i),
                                          textureByteSize,
                                          TT__BUFFER_OFFSET(streamOffset));
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

            glTextureSubImage3D(tt->cache.gl.textures[i],
                                0,
                                0, 0, page->textureID,
                                textureSize,
                                textureSize,
                                1,
                                format,
                                type,
                                TT__BUFFER_OFFSET(streamOffset));
        }
        pageDataOffset+= textureByteSize;
    }

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

static void tt__UpdateIndirectionBuffer(tt_Texture *tt)
{
    int32_t nodeCount = leb_NodeCount(tt->cache.leb);

    if (nodeCount <= tt->cache.capacity) {
        const GLuint *buffers = tt->updater.gl.buffers;
        int32_t *map = (int32_t *)glMapNamedBufferRange(
            buffers[TT__UPDATER_GL_BUFFER_INDIRECTION],
            0, sizeof(GLint) * tt->cache.capacity,
            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
        );

        for (int64_t i = 0; i < nodeCount; ++i) {
            leb_Node node = leb_DecodeNode(tt->cache.leb, i);
            const tt__Page *page = tt__LoadPage(tt, node.id);

            map[i] = page->textureID;
        }

        glUnmapNamedBuffer(buffers[TT__UPDATER_GL_BUFFER_INDIRECTION]);
        glCopyNamedBufferSubData(
            tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_LEB_CPU],
            tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_LEB],
            0, 0, 2 * sizeof(int32_t) + leb_HeapByteSize(tt->cache.leb)
        );
        glCopyNamedBufferSubData(
            tt->updater.gl.buffers[TT__UPDATER_GL_BUFFER_INDIRECTION],
            tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_INDIRECTION],
            0, 0, sizeof(GLint) * tt->cache.capacity
        );
    } else {
        TT_LOG("tt_Texture: too many nodes -- skipping this update");
    }
}

TTDEF void tt_Update(tt_Texture *tt, const tt_UpdateArgs *args)
{
    tt__UpdateLeb(tt, args);

    if (tt__LebAsynchronousReadBack(tt)) {
        tt__UpdateIndirectionBuffer(tt);
    }
}

TTDEF GLuint tt_LebBuffer(const tt_Texture *tt)
{
    return tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_LEB];
}

TTDEF GLuint tt_IndirectionBuffer(const tt_Texture *tt)
{
    return tt->cache.gl.buffers[TT__CACHE_GL_BUFFER_INDIRECTION];
}

TTDEF void tt_BindPageTextures(const tt_Texture *tt, GLenum *textureUnits)
{
    for (int64_t i = 0; i < tt_TexturesPerPage(tt); ++i) {
        glActiveTexture(textureUnits[i]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tt->cache.gl.textures[i]);
    }
}

#undef TT__BUFFER_SIZE
#undef TT__BUFFER_OFFSET

