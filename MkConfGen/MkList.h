#ifndef _MK_LIST_H
#define _MK_LIST_H

#include <assert.h>

typedef int BOOL;
#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE 1
#endif

typedef unsigned char byte;
typedef unsigned long ulong;

typedef struct MkList {
    ulong count;
    ulong capacity;
    ulong growCount;
    ulong elemSize;
    byte * elems;
} MkList;

BOOL MkListInit(MkList * listPtr, ulong elemSize, ulong growCount);
void MkListClear(MkList * listPtr);
void * MkListAppend(MkList * listPtr, ulong count);

static void * MkListGet(const MkList * listPtr, ulong index) {
    assert(listPtr);
    assert(listPtr->count <= listPtr->capacity);
    assert(listPtr->capacity != 0);
    assert(listPtr->growCount != 0);
    assert(listPtr->elemSize != 0);
    assert(listPtr->elems);
    assert(index < listPtr->count);

    return listPtr->elems + (index * listPtr->elemSize);
}

#endif