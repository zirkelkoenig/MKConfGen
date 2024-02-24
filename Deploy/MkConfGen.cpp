#include "MkConfGen.h"

#define _MKCONFGEN_ERRORS_GROW_COUNT 8

void _MkConfGenAddError(MkConfGenLoadError ** errorsPtr, unsigned long * errorCountPtr, MkConfGenLoadErrorType type, unsigned long line) {
    _MKCONFGEN_ASSERT(errorsPtr);
    _MKCONFGEN_ASSERT(errorCountPtr);

    if ((*errorCountPtr) % _MKCONFGEN_ERRORS_GROW_COUNT == 0) {
        unsigned long allocCount = *errorCountPtr + _MKCONFGEN_ERRORS_GROW_COUNT;
        *errorsPtr = (MkConfGenLoadError *)realloc(*errorsPtr, allocCount * sizeof(MkConfGenLoadError));
        _MKCONFGEN_ASSERT(*errorsPtr);
    }

    MkConfGenLoadError * errorPtr = &(*errorsPtr)[(*errorCountPtr)++];
    errorPtr->type = type;
    errorPtr->line = line;
}

bool _MkConfGenSkipLine(MkConfGenStreamNextCallback nextCallback, void * stream, wchar_t * nextWc, void ** stopStatus) {
    _MKCONFGEN_ASSERT(nextCallback);
    _MKCONFGEN_ASSERT(stream);
    _MKCONFGEN_ASSERT(nextWc);
    _MKCONFGEN_ASSERT(stopStatus);

    while (*nextWc != L'\n' && *nextWc != L'\r') {
        if (!nextCallback(stream, nextWc, stopStatus)) return false;
    }
    if (*nextWc == L'\n') {
        return nextCallback(stream, nextWc, stopStatus);
    } else {
        if (!nextCallback(stream, nextWc, stopStatus)) return false;
        if (*nextWc == L'\n') {
            return nextCallback(stream, nextWc, stopStatus);
        } else {
            return true;
        }
    }
}

void _MkConfGenLoad(
    MkConfGenStreamNextCallback nextCallback,
    void * stream,
    void ** stopStatus,
    unsigned long keyCount,
    const unsigned long * keyIndices,
    const wchar_t * keys,
    _MkConfGenParseValueCallback parseValueCallback,
    void * config,
    MkConfGenLoadError ** errors,
    unsigned long * errorCount)
{
    _MKCONFGEN_ASSERT(nextCallback);
    _MKCONFGEN_ASSERT(stream);
    _MKCONFGEN_ASSERT(stopStatus);
    _MKCONFGEN_ASSERT(keyIndices);
    _MKCONFGEN_ASSERT(parseValueCallback);
    _MKCONFGEN_ASSERT(keys);
    _MKCONFGEN_ASSERT(config);
    _MKCONFGEN_ASSERT(errors);
    _MKCONFGEN_ASSERT(errorCount);

    *errorCount = 0;
    *errors = NULL;

    wchar_t nextWc = L' ';
    unsigned long currentLine = 0;
    bool skipLine;

    wchar_t currentKey[MK_CONF_MAX_KEY_COUNT];
    unsigned long currentKeyLength;
    wchar_t currentRawValue[MK_CONF_MAX_VALUE_COUNT];
    unsigned long currentRawValueLength;
    bool valueIsStr;

    bool end = false;
    while (!end) {
        skipLine = false;
        currentKeyLength = 0;
        currentRawValueLength = 0;

        // Skip Whitespace and Newlines

        while (nextWc == L' ' || nextWc == L'\t' || nextWc == L'\n' || nextWc == L'\r') {
            if (!nextCallback(stream, &nextWc, stopStatus)) return;

            if (nextWc == L'\n') {
                if (!nextCallback(stream, &nextWc, stopStatus)) return;
                currentLine++;
            } else if (nextWc == L'\r') {
                if (!nextCallback(stream, &nextWc, stopStatus)) return;
                if (nextWc == L'\n') {
                    if (!nextCallback(stream, &nextWc, stopStatus)) return;
                }
                currentLine++;
            }
        }

        // Check First Key Char

        if (!(_MkConfGenWcIsAsciiLetter(nextWc) || nextWc == L'_')) {
            if (nextWc != L'#') {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_KEY_FORMAT, currentLine);
            }
            if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
            currentLine++;
            continue;
        }

        // Read Key Chars

        do {
            if (currentKeyLength == MK_CONF_MAX_KEY_COUNT - 1) {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_KEY_LENGTH, currentLine);
                if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
                currentLine++;
                skipLine = false;
                break;
            }
            currentKey[currentKeyLength++] = nextWc;

            if (!nextCallback(stream, &nextWc, stopStatus)) {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_NO_VALUE, currentLine);
                return;
            }
        } while (_MkConfGenWcIsAsciiLetter(nextWc) || iswdigit(nextWc) || nextWc == L'_');
        if (skipLine) {
            continue;
        }

        // Skip Whitespace

        while (nextWc == L' ' || nextWc == L'\t') {
            if (!nextCallback(stream, &nextWc, stopStatus)) {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_NO_VALUE, currentLine);
                return;
            }
        }

        // Check Equal Sign

        if (nextWc != L'=') {
            if (nextWc == L'#' || nextWc == L'\n' || nextWc == L'\r') {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_NO_VALUE, currentLine);
                if (nextWc == L'#') {
                    if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
                } else if (nextWc == L'\n') {
                    if (!nextCallback(stream, &nextWc, stopStatus)) return;
                } else {
                    if (!nextCallback(stream, &nextWc, stopStatus)) return;
                    if (nextWc == L'\n') {
                        if (!nextCallback(stream, &nextWc, stopStatus)) return;
                    }
                }
            } else {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_KEY_FORMAT, currentLine);
                if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
            }
            currentLine++;
            break;
        }

        // Skip Whitespace

        do {
            if (!nextCallback(stream, &nextWc, stopStatus)) {
                _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_NO_VALUE, currentLine);
                return;
            }
        } while (nextWc == L' ' || nextWc == L'\t');

        // Check First Value Char

        if (nextWc == L'#' || nextWc == L'\n' || nextWc == L'\r') {
            _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_NO_VALUE, currentLine);
            if (nextWc == L'#') {
                if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
            } else if (nextWc == L'\n') {
                if (!nextCallback(stream, &nextWc, stopStatus)) return;
            } else {
                if (!nextCallback(stream, &nextWc, stopStatus)) return;
                if (nextWc == L'\n') {
                    if (!nextCallback(stream, &nextWc, stopStatus)) return;
                }
            }
            currentLine++;
            break;
        }

        if (nextWc == L'\"') {
            // Read Raw String Value

            do {
                if (!nextCallback(stream, &nextWc, stopStatus)) {
                    _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_VALUE_FORMAT, currentLine);
                    return;
                }

                if (nextWc == L'\n' || nextWc == L'\r') {
                    _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_VALUE_FORMAT, currentLine);
                    if (nextWc == L'\n') {
                        if (!nextCallback(stream, &nextWc, stopStatus)) return;
                    } else {
                        if (!nextCallback(stream, &nextWc, stopStatus)) return;
                        if (nextWc == L'\n') {
                            if (!nextCallback(stream, &nextWc, stopStatus)) return;
                        }
                    }
                    currentLine++;
                    skipLine = false;
                    break;
                }

                if (currentRawValueLength == 512 - 1) {
                    _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_VALUE_LENGTH, currentLine);
                    if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
                    currentLine++;
                    skipLine = false;
                    break;
                }

                if (nextWc == L'\"') {
                    if (currentRawValueLength != 0 && currentRawValue[currentRawValueLength - 1] == L'\\') {
                        currentRawValue[currentRawValueLength - 1] = L'\"';
                        nextWc = L'\\';
                    }
                } else {
                    currentRawValue[currentRawValueLength++] = nextWc;
                }
            } while (nextWc != L'\"');
            if (skipLine) {
                continue;
            }

            end = !nextCallback(stream, &nextWc, stopStatus);
            valueIsStr = true;
        } else {
            // Read Raw Number Value

            while (!(end || nextWc == L' ' || nextWc == L'\t' || nextWc == L'\n' || nextWc == '\r')) {
                if (currentRawValueLength == MK_CONF_MAX_VALUE_COUNT - 1) {
                    _MkConfGenAddError(errors, errorCount, MKCONFGEN_LOAD_ERROR_VALUE_LENGTH, currentLine);
                    if (!_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus)) return;
                    currentLine++;
                    skipLine = false;
                    break;
                }
                currentRawValue[currentRawValueLength++] = nextWc;

                end = !nextCallback(stream, &nextWc, stopStatus);
            }
            if (skipLine) {
                continue;
            }

            valueIsStr = false;
        }

        // Discard Remainder

        if (!end) {
            end = !_MkConfGenSkipLine(nextCallback, stream, &nextWc, stopStatus);
        }

        // Parse

        currentKey[currentKeyLength] = L'\0';
        currentRawValue[currentRawValueLength] = L'\0';

        unsigned long i;
        for (i = 0; i != keyCount; i++) {
            unsigned long index = keyIndices[i];
            unsigned long length = (unsigned long)(keyIndices[i + 1] - index);
            if (wcsncmp(currentKey, keys + index, length) == 0) {
                break;
            }
        }
        if (i == keyCount) {
            continue;
        }

        MkConfGenLoadErrorType parseErrorType;
        if (!parseValueCallback(config, i, currentRawValue, currentRawValueLength, valueIsStr, &parseErrorType)) {
            _MkConfGenAddError(errors, errorCount, parseErrorType, currentLine);
        }

        currentLine++;
    }
}