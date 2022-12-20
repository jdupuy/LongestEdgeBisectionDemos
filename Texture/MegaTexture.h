
#ifndef MT_INCLUDE_MT_H
#define MT_INCLUDE_MT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MT_STATIC
#define MTDEF static
#else
#define MTDEF extern
#endif

// data type
typedef struct mt_Texture mt_Texture;

// ctor / dtor
MTDEF mt_Texture *mt_Create(int capacity);
MTDEF void mt_Release(mt_Texture *mt);

// manipulation
MTDEF void mt_Update(mt_Texture *mt,
                     const leb_Heap *leb,
                     uint64_t *indirectionTable);

#ifdef __cplusplus
} // extern "C"
#endif

//
//
//// end header file ///////////////////////////////////////////////////////////
#endif // MT_INCLUDE_MT_H

#if 1//def MT_IMPLEMENTATION

#ifndef MT_ASSERT
#    include <assert.h>
#    define MT_ASSERT(x) assert(x)
#endif

#ifndef MT_LOG
#    include <stdio.h>
#    define MT_LOG(format, ...) do { fprintf(stdout, format, ##__VA_ARGS__); fflush(stdout); } while(0)
#endif

#ifndef MT_MALLOC
#    include <stdlib.h>
#    define MT_MALLOC(x) (malloc(x))
#    define MT_FREE(x) (free(x))
#else
#    ifndef MT_FREE
#        error MT_MALLOC defined without MT_FREE
#    endif
#endif

#include "uthash.h"


struct mt__Chunk {
    uint32_t key;
    int textureID;

    UT_hash_handle hh;
};

struct mt__OpenGLTexture {
    GLuint64 handle;
    GLuint name;
};

struct mt_Texture {
    struct mt__Chunk            *cache;
    struct mt__OpenGLTexture    *textures;
    int capacity;
};


static void
mt__ProduceChunkTexture(mt_Texture *mt, const struct mt__Chunk *chunk)
{
    MT_LOG("Producing node %i using texture %i\n",
           chunk->key,
           mt->textures[chunk->textureID].name);
}


// This implements a LRU cache loading routine, where the requested chunk
// replaces the least recently used chunk stored in the cache whenever it's
// full.
static struct mt__Chunk *mt__LoadChunkFromDisk(mt_Texture *mt, uint32_t key)
{
    struct mt__Chunk *chunk = (struct mt__Chunk *)malloc(sizeof(*chunk));
    int cacheSize = HASH_COUNT(mt->cache);

    chunk->key = key;

    if (cacheSize < mt->capacity) {
        chunk->textureID = cacheSize;
    } else {
        struct mt__Chunk *lruChunk, *tmp;

        HASH_ITER(hh, mt->cache, lruChunk, tmp) {
            HASH_DELETE(hh, mt->cache, lruChunk);
            chunk->textureID = lruChunk->textureID;
            free(lruChunk);
            break;
        }
    }

    mt__ProduceChunkTexture(mt, chunk);

    HASH_ADD(hh, mt->cache, key, sizeof(key), chunk);

    return chunk;
}


// This function returns the chunk associated with a key.
static struct mt__Chunk *mt__LoadChunk(mt_Texture *mt, uint32_t key)
{
    struct mt__Chunk *chunk;

    HASH_FIND(hh, mt->cache, &key, sizeof(key), chunk);

    if (chunk) {
        HASH_DELETE(hh, mt->cache, chunk);
    } else {
        chunk = mt__LoadChunkFromDisk(mt, key);
    }

    HASH_ADD(hh, mt->cache, key, sizeof(key), chunk);

    return chunk;
}


/*
 * Update the mega texture
 *
 * This procedure updates the texture used for rendering.
 */
MTDEF void
mt_Update(mt_Texture *mt, const leb_Heap *leb, uint64_t *indirectionTable)
{
    for (int i = 0; i < leb_NodeCount(leb); ++i) {
        leb_Node node = leb_DecodeNode(leb, i);
        const struct mt__Chunk *chunk = mt__LoadChunk(mt, node.id);

        indirectionTable[i] = mt->textures[chunk->textureID].handle;
    }
}


static void mt__ReleaseCache(mt_Texture *mt)
{
   struct mt__Chunk *chunk, *tmp;

    HASH_ITER(hh, mt->cache, chunk, tmp) {
        HASH_DELETE(hh, mt->cache, chunk);
        free(chunk);
    }
}


static void mt__ReleaseTextures(mt_Texture *mt)
{
    for (int i = 0; i < mt->capacity; ++i) {
        glMakeTextureHandleNonResidentARB(mt->textures[i].handle);
        glDeleteTextures(1, &mt->textures[i].name);
    }
}


MTDEF void mt_Release(mt_Texture *mt)
{
    mt__ReleaseTextures(mt);
    mt__ReleaseCache(mt);
}


MTDEF mt_Texture *mt_Create(int capacity)
{
    MT_ASSERT(capacity > 0);
    mt_Texture *mt = (mt_Texture *)malloc(sizeof(*mt));

    mt->cache = NULL;
    mt->textures = (struct mt__OpenGLTexture *)MT_MALLOC(capacity * sizeof(*mt->textures));
    mt->capacity = capacity;

    return mt;
}


void mt__CreateTextures(mt_Texture *mt)
{
    const int textureSize = 1024;

    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < mt->capacity; ++i) {
        GLuint *texture = &mt->textures[i].name;

        glGenTextures(mt->capacity, texture);
        glTextureStorage2D(*texture, 1, GL_RGBA8, textureSize, textureSize);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(*texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        mt->textures[i].handle = glGetTextureHandleARB(*texture);
        glMakeTextureHandleResidentARB(mt->textures[i].handle);
    }
}



#endif
