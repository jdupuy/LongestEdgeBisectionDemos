
#include <cstdio>

#include "uthash.h"

typedef struct {
    int key;
    int data;

    UT_hash_handle hh;
} Chunk;

typedef struct {
    Chunk *data;      // data pointer
    int capacity;     // capacity
} ChunkMemory;

// load chunk inside the cache
void SaveChunk(ChunkMemory *memory, int key, int data)
{
    Chunk *chunk = (Chunk *)malloc(sizeof(*chunk));

    chunk->key  = key;
    chunk->data = data;

    if (HASH_COUNT(memory->data) == memory->capacity) {
        Chunk *chunk, *tmp;

        HASH_ITER(hh, memory->data, chunk, tmp) {
            HASH_DELETE(hh, memory->data, chunk);
            free(chunk);
            break;
        }
    }

    HASH_ADD(hh, memory->data, key, sizeof(key), chunk);  /* id: name of key field */
}

// load chunk either from the cache itself, or from the disk
Chunk *LoadChunk(ChunkMemory *memory, int key)
{
    Chunk *chunk;

    HASH_FIND(hh, memory->data, &key, sizeof(key), chunk);  /* s: output pointer */

    if (chunk) {
        HASH_DELETE(hh, memory->data, chunk);
        HASH_ADD(hh, memory->data, key, sizeof(key), chunk);
    } else {
        // produce chunk
    }

    return chunk;
}

void PrintCache(ChunkMemory *memory)
{
    Chunk *chunk, *tmp;

    HASH_ITER(hh, memory->data, chunk, tmp) {
        printf("data: %i %i\n", chunk->key, chunk->data);
    }
}

int main(int argc, char **argv)
{
    // create cache
    ChunkMemory memory = {NULL, 2};

    // add elements
    SaveChunk(&memory, 25, 4);
    SaveChunk(&memory, 32, 1);
    SaveChunk(&memory, 48, 545);
    //SaveChunk(&memory, 64, 4);

    // try to load some chunks
    Chunk *chunk1 = LoadChunk(&memory, 25);
    Chunk *chunk2 = LoadChunk(&memory, 48);
    Chunk *chunk3 = LoadChunk(&memory, 32);

    // print data
    if (chunk1) printf("%i %i\n", chunk1->key, chunk1->data);
    if (chunk2) printf("%i %i\n", chunk2->key, chunk2->data);
    if (chunk3) printf("%i %i\n", chunk3->key, chunk3->data);
    fflush(stdout);

#if 1 // check if LRU actually works
    PrintCache(&memory);
    LoadChunk(&memory, 48);
    PrintCache(&memory);
#endif


    // cleanup
    Chunk *chunk, *tmp;
    HASH_ITER(hh, memory.data, chunk, tmp) {
        HASH_DELETE(hh, memory.data, chunk);  /* delete; users advances to next */
        free(chunk);                          /* optional- if you want to free  */
    }



    return 0;
}

