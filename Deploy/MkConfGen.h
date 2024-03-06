/*
MIT License

Copyright (c) 2024 Marvin Kipping

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
#define MKCONFGEN_ITEM_FLOAT(itemName, defaultValue) double itemName = defaultValue;
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
#define MKCONFGEN_ITEM_FLOAT(itemName, defaultValue)
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
    size_t line;
} MkConfGenLoadError;

typedef bool (*_MkConfGenParseValueCallback)(
    void * config,
    size_t index,
    wchar_t * rawValue,
    size_t rawValueLength,
    bool isStr,
    MkConfGenLoadErrorType * errorType);

bool _MkConfGenLoad(
    const wchar_t * configWcs,
    size_t configLength,
    size_t keyCount,
    const size_t * keyIndices,
    const wchar_t * keys,
    _MkConfGenParseValueCallback parseValueCallback,
    void * config,
    MkConfGenLoadError ** errors,
    size_t * errorCount);

#endif