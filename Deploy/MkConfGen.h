#ifndef _MKCONFGEN_H
#define _MKCONFGEN_H

#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#ifndef NDEBUG
#include <assert.h>

#ifdef MKCONFGEN_ASSERT_ENABLE
#define _MKCONFGEN_ASSERT(E) assert(E)
#else
#define _MKCONFGEN_ASSERT(E)
#endif

#define MKCONFGEN_FILE_BEGIN
#define MKCONFGEN_FILE_END

#define MKCONFGEN_DEF_BEGIN(confName) void _MkConfGenCheck##confName() { bool validateResult;
#define MKCONFGEN_DEF_END }

#define MKCONFGEN_HEADING(headingName)

#define MKCONFGEN_ITEM_INT(itemName, defaultValue) long itemName = defaultValue;
#define MKCONFGEN_ITEM_UINT(itemName, defaultValue) unsigned long itemName = defaultValue;
#define MKCONFGEN_ITEM_WSTR(itemName, count, defaultValue) wchar_t itemName[count] = defaultValue;

#define MKCONFGEN_VALIDATE(itemName, callback) validateResult = callback(itemName);
#else
#define _MKCONFGEN_ASSERT(E)

#define MKCONFGEN_FILE_BEGIN
#define MKCONFGEN_FILE_END

#define MKCONFGEN_DEF_BEGIN(confName)
#define MKCONFGEN_DEF_END

#define MKCONFGEN_HEADING(headingName)

#define MKCONFGEN_ITEM_INT(itemName, defaultValue)
#define MKCONFGEN_ITEM_UINT(itemName, defaultValue)
#define MKCONFGEN_ITEM_WSTR(itemName, count, defaultValue)

#define MKCONFGEN_VALIDATE(itemName, callback)
#endif

#define MK_CONF_MAX_KEY_COUNT 64
#define MK_CONF_MAX_VALUE_COUNT 512

typedef enum MkConfGenLoadErrorType {
    MKCONFGEN_LOAD_ERROR_UNDEFINED,
    MKCONFGEN_LOAD_ERROR_KEY_FORMAT, // The key is malformed.
    MKCONFGEN_LOAD_ERROR_KEY_LENGTH, // The key is too long.
    MKCONFGEN_LOAD_ERROR_NO_VALUE, // The line contains a key but no value.
    MKCONFGEN_LOAD_ERROR_FORMAT, // The line is malformed.
    MKCONFGEN_LOAD_ERROR_VALUE_FORMAT, // The value is malformed.
    MKCONFGEN_LOAD_ERROR_VALUE_LENGTH, // The value is too long.
    MKCONFGEN_LOAD_ERROR_VALUE_TYPE, // The value has the wrong type.
    MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW, // The numeric value is out of bounds or the string value is too long.
    MKCONFGEN_LOAD_ERROR_VALUE_INVALID, // The value is invalid.
} MkConfGenLoadErrorType;

typedef struct MkConfGenLoadError {
    MkConfGenLoadErrorType type;
    unsigned long line;
} MkConfGenLoadError;

typedef bool (*_MkConfGenParseValueCallback)(
    void * config,
    unsigned long index,
    wchar_t * rawValue,
    unsigned long rawValueLength,
    bool isStr,
    MkConfGenLoadErrorType * errorType);

bool _MkConfGenLoad(
    const wchar_t * configWcs,
    unsigned long configLength,
    unsigned long keyCount,
    const unsigned long * keyIndices,
    const wchar_t * keys,
    _MkConfGenParseValueCallback parseValueCallback,
    void * config,
    MkConfGenLoadError ** errors,
    unsigned long * errorCount);

#endif