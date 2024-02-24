#include <assert.h>
#include <stdlib.h>

#include "MkWStr.h"

MkWstr * MkWstrValAlloc(ulong length) {
    MkWstr * wstr = malloc(sizeof(MkWstr) + (length + 1) * sizeof(wchar_t));
    if (!wstr) {
        return NULL;
    }
    wstr->length = length;
    wstr->valInside = TRUE;
    wstr->wcs = (wchar_t *)(wstr + 1);
    wstr->wcs[length] = L'\0';
    return wstr;
}

MkWstr * MkWstrValRealloc(MkWstr * wstr, ulong length) {
    assert(wstr);
    assert(wstr->valInside);

    MkWstr * newWstr = realloc(wstr, sizeof(MkWstr) + (length + 1) * sizeof(wchar_t));
    if (!newWstr) {
        return NULL;
    }
    newWstr->length = length;
    newWstr->wcs = (wchar_t *)(wstr + 1);
    newWstr->wcs[length] = L'\0';
    return newWstr;
}

ulong MkWstrFindSubstrIndexWc(const MkWstr * wstr, const wchar_t * substrWc) {
    assert(wstr);
    assert(substrWc);

    size_t substrLength = wcslen(substrWc);
    if (substrLength == 0) {
        return 0;
    }
    if (substrLength > wstr->length) {
        return ULONG_MAX;
    }
    ulong end = wstr->length - substrLength + 1;
    for (ulong i = 0; i != end; i++) {
        BOOL match = TRUE;
        for (ulong j = 0; j != substrLength; j++) {
            if (wstr->wcs[i + j] != substrWc[j]) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return ULONG_MAX;
}

ulong MkWstrFindSubstrIndexWcNoCase(const MkWstr * wstr, const wchar_t * substrWc) {
    assert(wstr);
    assert(substrWc);

    size_t substrLength = wcslen(substrWc);
    if (substrLength == 0) {
        return 0;
    }
    if (substrLength > wstr->length) {
        return ULONG_MAX;
    }
    ulong end = wstr->length - substrLength + 1;
    for (ulong i = 0; i != end; i++) {
        BOOL match = TRUE;
        for (ulong j = 0; j != substrLength; j++) {
            wchar_t upperA = towupper(wstr->wcs[i + j]);
            wchar_t upperB = towupper(substrWc[j]);
            if (upperA != upperB) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return ULONG_MAX;
}

ulong MkWstrFindCharsNextIndex(const MkWstr * wstr, ulong startIndex, const wchar_t wcs[], ulong wcsCount) {
    assert(wstr);
    assert(startIndex < wstr->length);
    assert(wcs);

    for (ulong i = startIndex; i != wstr->length; i++) {
        for (ulong j = 0; j != wcsCount; j++) {
            if (wstr->wcs[i] == wcs[j]) {
                return i;
            }
        }
    }
    return ULONG_MAX;
}

BOOL MkWstrIsPrefixWc(const MkWstr * wstr, ulong startIndex, const wchar_t * prefixWc) {
    assert(wstr);
    assert(startIndex < wstr->length);
    assert(prefixWc);

    const wchar_t * wc = wstr->wcs + startIndex;
    const wchar_t * end = wstr->wcs + wstr->length;

    while (wc != end && *prefixWc != L'\0') {
        if ((*wc++) != (*prefixWc++)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL MkWstrsAreEqual(const MkWstr * a, const MkWstr * b) {
    assert(a);
    assert(b);

    if (a->length != b->length) {
        return FALSE;
    }
    ulong i = 0;
    while (i != a->length && i != b->length) {
        if (a->wcs[i] != b->wcs[i]) {
            return FALSE;
        }
        i++;
    }
    return TRUE;
}