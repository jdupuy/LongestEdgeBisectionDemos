
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

// data type
typedef struct tt_Texture tt_Texture;

// ctor / dtor
TTDEF tt_Texture *tt_Load(const char *filename, int cacheByteSize);
TTDEF void tt_Release(tt_Texture *tt);

// creates a file
bool tt_Create(const char *file, int mebiResolution, int pageSize, tt_Format format);

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


typedef struct {
    uint32_t key;
    int32_t textureID;

    UT_hash_handle hh;
} tt__TextureMapper;

enum tt_Format {
    TT_FORMAT_RGB, // red green blue
    TT_FORMAT_PBR  // displacement, normals, albedo
};

struct tt_Texture {
    struct {
        FILE *stream;               // pointer to file
        struct {
            int res;                // side resolution of the pages
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

// resolution
int tt__Resolution(const tt_Texture *tt)
{
    return 1 << ((tt->storage.depth - 1) / 2);
}

// number of textures in cache
/*int tt__CacheTextureSize(const tt_Texture *tt)
{
    return tt__Resolution(tt);
}*/

/*
struct tt_UpdateParameters {
    float *modelViewMatrix;
    float *modelViewProjectionMatrix;
    float texelLengthTarget;
    float screenHeight;
    float fieldOfView;
};
*/

tt_Texture *tt_Load(const char *file);
bool tt_Create(const char *file, int mebiResolution, int pageSize, enum tt_Format format);


void tt_Update(tt_Texture *tt, float *modelViewMatrix, float *modelViewProjection);

#if 0
// Memory manager
struct tt_Texture {
    struct /* fast memory */ {
        tt__MapItem *map;           // LRU map
        struct {
            GLuint      *textures;  // texture names
            GLuint64    *handles;   // texture handles
            GLuint      *buffers;   // handles (UBO) + LEB Heap (SSBO)
        } gl;
        int capacity;
    } cache;
    struct /* slow memory */ {
        FILE *stream;           // pointer to file
        int capacity;           // capacity in Bytes
        int chunkResolution;    // resolution of a chunk
    } memory;
};
#endif

// Queries
bool tt_CreateTexture(const char *file,
                      int mebiResolution,
                      int chunkResolution);
bool tt_IsValidTexture(const char *file);
bool tt_GetTextureInfo(const char *file, int *resolution, int *chunkResolution);

// Loaders
tt_Texture *tt_LoadTextureRO(const char *file);
tt_Texture *tt_LoadTextureRW(const char *file);

//
tt_Texture *tt__LoadTexture(const char *file, const char *mode)
{
    tt_Texture *tt;

    TT_LOG("opening file...");
    tt->memory.stream = fopen(file, mode);
    TT_ASSERT(tt->memory.stream && "fopen failed");

    TT_LOG("checking header...");
}

// Dtor
void tt_Release(tt_Texture *tt);

// Queries
int tt_TextureWidth(const tt_Texture *tt);
int tt_TextureHeight(const tt_Texture *tt);

// Updates the cache
struct tt_TextureReader {

};

// Updates the data
struct tt_TextureWriter {
    tt_Texture *tt;         // reference to the texture
    struct {
        GLuint *textures;   // brush textures
        GLuint *programs;   // blit programs
        GLuint *samplers;   // sampler types
        //GLuint *;
    } gl;
}


