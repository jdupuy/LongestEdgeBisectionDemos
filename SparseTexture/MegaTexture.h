
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
MTDEF mt_Texture *mt_Create(int cacheCapacity, int streamByteSize);
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
    struct mt__Chunk            *cache;     // LRU cache
    struct mt__OpenGLTexture    *textures;  // fast memory
    struct {
        GLuint name;
        int size, offset;
    } buffer;                               // asynchronous streaming buffer
    int capacity;                           // fast memory capacity
};


static void
mt__ProduceChunkTexture(mt_Texture *mt, const struct mt__Chunk *chunk)
{
    MT_LOG("Producing node %i using texture %i (%i)\n",
           chunk->key,
           chunk->textureID,
           mt->textures[chunk->textureID].name);
    int byteSize = 256 * 256 * 4;
    uint8_t *data;

    if (mt->buffer.offset + byteSize > mt->buffer.size) {
        mt->buffer.offset = 0;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mt->buffer.name);
    data = (uint8_t *)glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER,
        mt->buffer.offset,
        byteSize,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT
    );

    for (int i = 0; i < 256 * 256; ++i) {
        data[4 * i    ] = 255u;
        data[4 * i + 1] = 255u;
        data[4 * i + 2] = 255u;
        data[4 * i + 3] = 255u;
    }

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTextureSubImage2D(mt->textures[chunk->textureID].name,
                        0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE,
                        (char *)NULL + mt->buffer.offset);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    mt->buffer.offset+= byteSize;
}


// This implements a LRU cache loading routine, where the requested chunk
// replaces the least recently used chunk stored in the cache whenever it's
// full.
static struct mt__Chunk *mt__LoadChunkFromDisk(mt_Texture *mt, uint32_t key)
{
    struct mt__Chunk *chunk = (struct mt__Chunk *)malloc(sizeof(*chunk));
    int cacheSize = HASH_COUNT(mt->cache);

    MT_LOG("Cache Size: %i\n", cacheSize);

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


static void mt__ReleaseBuffer(mt_Texture *mt)
{
    glDeleteBuffers(1, &mt->buffer.name);
}


static void mt__CreateTextures(mt_Texture *mt)
{
    const int textureSize = 256;

    glActiveTexture(GL_TEXTURE0 + 64);
    for (int i = 0; i < mt->capacity; ++i) {
        GLuint *texture = &mt->textures[i].name;

        glGenTextures(1, texture);
        glBindTexture(GL_TEXTURE_2D, *texture);
        glTextureStorage2D(*texture, 1, GL_RGBA8, textureSize, textureSize);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(*texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        mt->textures[i].handle = glGetTextureHandleARB(*texture);
        glMakeTextureHandleResidentARB(mt->textures[i].handle);

        glBindTexture(GL_TEXTURE_2D, 0);
    }
}


static void mt__CreateBuffer(mt_Texture *mt)
{
    glGenBuffers(1, &mt->buffer.name);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mt->buffer.name);
    glBufferStorage(GL_PIXEL_UNPACK_BUFFER,
                    mt->buffer.size,
                    NULL,
                    GL_MAP_WRITE_BIT);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}


MTDEF void mt_Release(mt_Texture *mt)
{
    mt__ReleaseTextures(mt);
    mt__ReleaseCache(mt);
}


MTDEF mt_Texture *mt_Create(int cacheCapacity, int streamByteSize)
{
    MT_ASSERT(cacheCapacity > 0);
    mt_Texture *mt = (mt_Texture *)malloc(sizeof(*mt));

    mt->cache = NULL;
    mt->textures = (struct mt__OpenGLTexture *)MT_MALLOC(
        cacheCapacity * sizeof(*mt->textures)
    );
    mt->capacity = cacheCapacity;
    mt->buffer.size = streamByteSize;
    mt->buffer.offset = 0;

    mt__CreateTextures(mt);
    mt__CreateBuffer(mt);

    return mt;
}





#endif
