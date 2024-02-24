#include "MkList.h"

#include <stdlib.h>

BOOL MkListInit(MkList * listPtr, ulong elemSize, ulong growCount) {
    assert(listPtr);
    assert(growCount != 0);

    listPtr->count = 0;
    listPtr->capacity = growCount;
    listPtr->growCount = growCount;
    listPtr->elemSize = elemSize;
    listPtr->elems = malloc(growCount * elemSize);

    return listPtr->elems != NULL;
}


void MkListClear(MkList * listPtr) {
    assert(listPtr);
    assert(listPtr->elems);

    listPtr->count = 0;
    listPtr->capacity = 0;
    listPtr->growCount = 0;
    listPtr->elemSize = 0;
    free(listPtr->elems);
}

void * MkListAppend(MkList * listPtr, ulong count) {
    assert(listPtr);
    assert(listPtr->count <= listPtr->capacity);
    assert(listPtr->capacity != 0);
    assert(listPtr->growCount != 0);
    assert(listPtr->elemSize != 0);
    assert(listPtr->elems);
    assert(count != 0);

    if (listPtr->count == listPtr->capacity) {
        ulong newCapacity = listPtr->capacity + listPtr->growCount;
        byte * newElems = realloc(listPtr->elems, newCapacity * listPtr->elemSize);
        if (!newElems) {
            return NULL;
        }
        listPtr->capacity = newCapacity;
        listPtr->elems = newElems;
    }

    byte * newElemsPtr = listPtr->elems + (listPtr->count * listPtr->elemSize);
    listPtr->count += count;
    return newElemsPtr;
}