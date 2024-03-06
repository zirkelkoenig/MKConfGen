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

#include "MkConfGen.h"

#define _MKCONFGEN_ERRORS_GROW_COUNT 8

bool _MkConfGenLoad(
    const wchar_t * configWcs,
    size_t configLength,
    size_t keyCount,
    const size_t * keyIndices,
    const wchar_t * keys,
    _MkConfGenParseValueCallback parseValueCallback,
    void * config,
    MkConfGenLoadError ** errors,
    size_t * errorCount)
{
    _MKCONFGEN_ASSERT(configWcs || configLength == 0);
    _MKCONFGEN_ASSERT(keyIndices);
    _MKCONFGEN_ASSERT(parseValueCallback);
    _MKCONFGEN_ASSERT(keys);
    _MKCONFGEN_ASSERT(config);
    _MKCONFGEN_ASSERT(errors);
    _MKCONFGEN_ASSERT(errorCount);

    *errorCount = 0;
    *errors = NULL;
    bool memoryError = false;

    size_t currentLine = 0;
    bool skipLine;

    wchar_t currentKey[MK_CONF_MAX_KEY_COUNT];
    size_t currentKeyLength;
    wchar_t currentRawValue[MK_CONF_MAX_VALUE_COUNT];
    size_t currentRawValueLength;
    bool valueIsStr;

    size_t i = 0;

    auto SkipLine = [&configWcs, &configLength, &i]() {
        while (configWcs[i] != L'\n') {
            if (++i == configLength) return false;
        }
        return ++i != configLength;
    };

    auto IsAsciiLetter = [&configWcs, &i]() {
        return (configWcs[i] >= L'A' && configWcs[i] <= L'Z') || (configWcs[i] >= L'a' && configWcs[i] <= L'z');
    };

    auto AddError = [&errors, &errorCount, &currentLine, &memoryError](MkConfGenLoadErrorType type) {
        if ((*errorCount) % _MKCONFGEN_ERRORS_GROW_COUNT == 0) {
            size_t allocCount = *errorCount + _MKCONFGEN_ERRORS_GROW_COUNT;
            MkConfGenLoadError * newErrors = (MkConfGenLoadError *)realloc(*errors, allocCount * sizeof(MkConfGenLoadError));
            if (!newErrors) {
                memoryError = true;
                return;
            }
            *errors = newErrors;
            _MKCONFGEN_ASSERT(*errors);
        }

        MkConfGenLoadError * errorPtr = &(*errors)[(*errorCount)++];
        errorPtr->type = type;
        errorPtr->line = currentLine;
    };

    while (i != configLength) {
        skipLine = false;
        currentKeyLength = 0;
        currentRawValueLength = 0;

        // Skip Whitespace and Newlines

        while (configWcs[i] == L' ' || configWcs[i] == L'\t' || configWcs[i] == L'\n') {
            if (++i == configLength) return memoryError;
            if (configWcs[i] == L'\n') {
                if (++i == configLength) return memoryError;
                currentLine++;
            }
        }

        // Check First Key Char

        if (!(IsAsciiLetter() || configWcs[i] == L'_')) {
            if (configWcs[i] != L'#') {
                AddError(MKCONFGEN_LOAD_ERROR_KEY_FORMAT);
            }
            if (!SkipLine()) return memoryError;
            currentLine++;
            continue;
        }

        // Read Key Chars

        do {
            if (currentKeyLength == MK_CONF_MAX_KEY_COUNT - 1) {
                AddError(MKCONFGEN_LOAD_ERROR_KEY_LENGTH);
                if (!SkipLine()) return memoryError;
                currentLine++;
                skipLine = false;
                break;
            }
            currentKey[currentKeyLength++] = configWcs[i];

            if (++i == configLength) {
                AddError(MKCONFGEN_LOAD_ERROR_NO_VALUE);
                return memoryError;
            }
        } while (IsAsciiLetter() || iswdigit(configWcs[i]) || configWcs[i] == L'_');
        if (skipLine) {
            continue;
        }

        // Skip Whitespace

        while (configWcs[i] == L' ' || configWcs[i] == L'\t') {
            if (++i == configLength) {
                AddError(MKCONFGEN_LOAD_ERROR_NO_VALUE);
                return memoryError;
            }
        }

        // Check Equal Sign

        if (configWcs[i] != L'=') {
            if (configWcs[i] == L'#' || configWcs[i] == L'\n') {
                AddError(MKCONFGEN_LOAD_ERROR_NO_VALUE);
                if (configWcs[i] == L'#') {
                    if (!SkipLine()) return memoryError;
                } else {
                    if (++i == configLength) return memoryError;
                }
            } else {
                AddError(MKCONFGEN_LOAD_ERROR_KEY_FORMAT);
                if (!SkipLine()) return memoryError;
            }
            currentLine++;
            break;
        }

        // Skip Whitespace

        do {
            if (++i == configLength) {
                AddError(MKCONFGEN_LOAD_ERROR_NO_VALUE);
                return memoryError;
            }
        } while (configWcs[i] == L' ' || configWcs[i] == L'\t');

        // Check First Value Char

        if (configWcs[i] == L'#' || configWcs[i] == L'\n') {
            AddError(MKCONFGEN_LOAD_ERROR_NO_VALUE);
            if (configWcs[i] == L'#') {
                if (!SkipLine()) return memoryError;
            } else {
                if (++i == configLength) return memoryError;
            }
            currentLine++;
            break;
        }

        if (configWcs[i] == L'\"') {
            // Read Raw String Value

            while (true) {
                if (++i == configLength) {
                    AddError(MKCONFGEN_LOAD_ERROR_VALUE_FORMAT);
                    return memoryError;
                }

                if (configWcs[i] == L'\n') {
                    AddError(MKCONFGEN_LOAD_ERROR_VALUE_FORMAT);
                    if (++i == configLength) return memoryError;
                    currentLine++;
                    skipLine = false;
                    break;
                }

                if (configWcs[i] == L'\"') {
                    if (currentRawValueLength != 0 && currentRawValue[currentRawValueLength - 1] == L'\\') {
                        currentRawValue[currentRawValueLength - 1] = L'\"';
                        continue;
                    } else {
                        break;
                    }
                }

                if (currentRawValueLength == MK_CONF_MAX_VALUE_COUNT - 1) {
                    AddError(MKCONFGEN_LOAD_ERROR_VALUE_LENGTH);
                    if (!SkipLine()) return memoryError;
                    currentLine++;
                    skipLine = false;
                    break;
                }

                currentRawValue[currentRawValueLength++] = configWcs[i];
            }
            if (skipLine) {
                continue;
            }

            i++;
            valueIsStr = true;
        } else {
            // Read Raw Number Value

            while (!(i == configLength || configWcs[i] == L' ' || configWcs[i] == L'\t' || configWcs[i] == L'\n')) {
                if (currentRawValueLength == MK_CONF_MAX_VALUE_COUNT - 1) {
                    AddError(MKCONFGEN_LOAD_ERROR_VALUE_LENGTH);
                    if (!SkipLine()) return memoryError;
                    currentLine++;
                    skipLine = false;
                    break;
                }
                currentRawValue[currentRawValueLength++] = configWcs[i];

                i++;
            }
            if (skipLine) {
                continue;
            }

            valueIsStr = false;
        }

        // Discard Remainder

        if (i != configLength) {
            SkipLine();
        }

        // Parse

        currentKey[currentKeyLength] = L'\0';
        currentRawValue[currentRawValueLength] = L'\0';

        size_t j;
        for (j = 0; j != keyCount; j++) {
            size_t index = keyIndices[j];
            size_t length = keyIndices[j + 1] - index;
            if (wcsncmp(currentKey, keys + index, length) == 0) {
                break;
            }
        }
        if (j == keyCount) {
            continue;
        }

        MkConfGenLoadErrorType parseErrorType;
        if (!parseValueCallback(config, j, currentRawValue, currentRawValueLength, valueIsStr, &parseErrorType)) {
            AddError(parseErrorType);
        }

        currentLine++;
    }

    return !memoryError;
}