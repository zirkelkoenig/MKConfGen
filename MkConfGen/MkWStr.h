#ifndef _MK_WSTR_H
#define _MK_WSTR_H

#include <wchar.h>

typedef int BOOL;
#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE 1
#endif

typedef unsigned long ulong;

typedef struct MkWstr {
    ulong length;
    BOOL valInside;
    wchar_t * wcs;
} MkWstr;

MkWstr * MkWstrValAlloc(ulong length);

MkWstr * MkWstrValRealloc(MkWstr * wstr, ulong length);

static void MkWstrRefInit(MkWstr * wstrPtr, ulong length, const wchar_t * wcs) {
    assert(wstrPtr);
    assert(wcs);

    wstrPtr->length = length;
    wstrPtr->valInside = FALSE;
    wstrPtr->wcs = wcs;
}

ulong MkWstrFindSubstrIndexWc(const MkWstr * wstr, const wchar_t * substrWc);

ulong MkWstrFindSubstrIndexWcNoCase(const MkWstr * wstr, const wchar_t * substrWc);

ulong MkWstrFindCharsNextIndex(const MkWstr * wstr, ulong startIndex, const wchar_t wcs[], ulong wcsCount);

BOOL MkWstrIsPrefixWc(const MkWstr * wstr, ulong startIndex, const wchar_t * prefixWc);

BOOL MkWstrsAreEqual(const MkWstr * a, const MkWstr * b);

#endif