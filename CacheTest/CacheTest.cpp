
#include <cstdio>

#include "uthash.h"


typedef struct {
    int key;
    int data;

    UT_hash_handle hh;
} Chunk;

typedef struct {
    Chunk *data;            // data pointer
    const int capacity;     // capacity
} ChunkMemory;

// load chunk inside the cache
void SaveChunk(ChunkMemory *memory, int key, int data)
{
    Chunk *chunk = (Chunk *)malloc(sizeof(*chunk));

    chunk->key  = key;
    chunk->data = data;

    HASH_ADD(hh, memory->data, key, sizeof(key), chunk);  /* id: name of key field */
}

// load chunk either from the cache itself, or from the disk
Chunk *LoadChunk(ChunkMemory *memory, int key)
{
    Chunk *chunk;

    HASH_FIND(hh, memory->data, &key, sizeof(key), chunk);  /* s: output pointer */

    return chunk;
}

int main(int argc, char **argv)
{
    // create cache
    ChunkMemory memory = {NULL, 16};

    // add elements
    SaveChunk(&memory, 32, 1);
    SaveChunk(&memory, 25, 4);

    // try to load some chunks
    Chunk *chunk1 = LoadChunk(&memory, 32);
    Chunk *chunk2 = LoadChunk(&memory, 25);
    Chunk *chunk3 = LoadChunk(&memory, 46);

    // print data
    if (chunk1) printf("%i %i\n", chunk1->key, chunk1->data);
    if (chunk2) printf("%i %i\n", chunk2->key, chunk2->data);
    if (chunk3) printf("%i %i\n", chunk3->key, chunk3->data);
    fflush(stdout);

    // cleanup
    Chunk *chunk, *tmp;
    HASH_ITER(hh, memory.data, chunk, tmp) {
        HASH_DELETE(hh, memory.data, chunk);  /* delete; users advances to next */
        free(chunk);                          /* optional- if you want to free  */
    }

    return 0;
}

