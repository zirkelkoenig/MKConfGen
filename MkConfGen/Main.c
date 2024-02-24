#include <Windows.h>

#include <assert.h>
#include <wchar.h>

#include "MkList.h"
#include "MkWStr.h"

typedef int bool;
#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE 1
#endif

typedef unsigned char byte;
typedef unsigned short ushort;
typedef unsigned long ulong;

typedef enum Utf8State {
    UTF8_START,
    UTF8_END_OK,
    UTF8_END_ERROR,
    UTF8_READ_1,
    UTF8_READ_2,
    UTF8_READ_3,
    UTF8_READ_ERROR,
} Utf8State;

typedef struct Heading {
    ulong index;
    MkWstr name;
} Heading;

typedef enum ItemType {
    ITEM_NONE,
    ITEM_INT,
    ITEM_UINT,
    ITEM_WSTR,
} ItemType;

typedef struct Item {
    ItemType type;
    MkWstr name;
    MkWstr length; // WSTR only
    MkWstr defaultValue;
    MkWstr validateCallback;
} Item;

typedef struct Config {
    MkWstr name;
    MkList headings;
    MkList items;
} Config;

typedef enum ParseState {
    PARSE_FILE,
    PARSE_DEF_BEGIN_KEYWORD,
    PARSE_DEF_BEGIN_OPEN,
    PARSE_DEF_BEGIN_NAME,
    PARSE_DEF,
    PARSE_HEADING_KEYWORD,
    PARSE_HEADING_OPEN,
    PARSE_HEADING_NAME,
    PARSE_WSTR_KEYWORD,
    PARSE_WSTR_OPEN,
    PARSE_WSTR_NAME,
    PARSE_WSTR_NAME_SEP,
    PARSE_WSTR_COUNT,
    PARSE_WSTR_COUNT_SEP,
    PARSE_WSTR_DEFAULT,
    PARSE_INT_KEYWORD,
    PARSE_INT_OPEN,
    PARSE_INT_NAME,
    PARSE_INT_SEP,
    PARSE_INT_DEFAULT,
    PARSE_UINT_KEYWORD,
    PARSE_UINT_OPEN,
    PARSE_UINT_NAME,
    PARSE_UINT_SEP,
    PARSE_UINT_DEFAULT,
    PARSE_VALIDATE_KEYWORD,
    PARSE_VALIDATE_OPEN,
    PARSE_VALIDATE_NAME,
    PARSE_VALIDATE_SEP,
    PARSE_VALIDATE_CALLBACK,
    PARSE_STOP,
} ParseState;

const wchar_t fullTokenFileBegin[] = L"MKCONFGEN_FILE_BEGIN";

const wchar_t tokenPrefix[] = L"MKCONFGEN_";

const wchar_t tokenFileEnd[] = L"FILE_END";
const wchar_t tokenDefBegin[] = L"DEF_BEGIN";
const wchar_t tokenDefEnd[] = L"DEF_END";
const wchar_t tokenHeading[] = L"HEADING";
const wchar_t tokenValidate[] = L"VALIDATE";

const wchar_t tokenPrefixItem[] = L"ITEM_";
const wchar_t tokenItemWstr[] = L"WSTR";
const wchar_t tokenItemInt[] = L"INT";
const wchar_t tokenItemUint[] = L"UINT";

const wchar_t closeChars[] = { L' ', L'\t', L'\n', L')' };
const wchar_t sepChars[] = { L' ', L'\t', L'\n', L',' };

#define ConsumeWhitespace(str, i) while ((str)[i] == L' ' || (str)[i] == L'\t' || (str)[i] == L'\n') i++

bool AppendWc(MkWstr ** wstr, size_t * i, wchar_t wc) {
    if (*i == (*wstr)->length) {
        MkWstr * newWstr = MkWstrValRealloc(*wstr, (*wstr)->length * 2);
        if (!newWstr) {
            return FALSE;
        }
        *wstr = newWstr;
    }

    (*wstr)->wcs[(*i)++] = wc;
    return TRUE;
}

void WriteUtf8Wc(HANDLE file, wchar_t wc) {
    byte output[4];
    ushort outputCount;
    ulong c = wc;

    if (c <= 0x007f) {
        output[0] = (byte)c;
        outputCount = 1;
    } else if (c <= 0x07ff) {
        output[0] = (byte)((c >> 6) + 0b11000000);
        output[1] = (byte)((c & 0b00111111) + 0b10000000);
        outputCount = 2;
    } else if (c <= 0xffff) {
        output[0] = (byte)((c >> 12) + 0b11100000);
        output[1] = (byte)((c >> 6 & 0b00111111) + 0b10000000);
        output[2] = (byte)((c & 0b00111111) + 0b10000000);
        outputCount = 3;
    } else if (c <= 0x0010ffff) {
        output[0] = (byte)((c >> 18) + 0b11110000);
        output[1] = (byte)((c >> 12 & 0b00111111) + 0b10000000);
        output[2] = (byte)((c >> 6 & 0b00111111) + 0b10000000);
        output[3] = (byte)((c & 0b00111111) + 0b10000000);
        outputCount = 4;
    } else {
        // 0xFFFD
        output[0] = 0b11101111;
        output[1] = 0b10111111;
        output[2] = 0b10111101;
        outputCount = 3;
    }

    ulong writeCount;
    WriteFile(file, output, outputCount, &writeCount, NULL);
}

void WriteUtf8Wcsn(HANDLE file, const wchar_t * wcs, ulong count) {
    for (ulong i = 0; i != count; i++) {
        if (wcs[i] == L'\n') {
            WriteUtf8Wc(file, L'\r');
            WriteUtf8Wc(file, L'\n');
        } else {
            WriteUtf8Wc(file, wcs[i]);
        }
    }
}

void WriteUtf8Wcs(HANDLE file, const wchar_t * wcs) {
    while (*wcs != L'\0') {
        if (*wcs == L'\n') {
            WriteUtf8Wc(file, L'\r');
            WriteUtf8Wc(file, L'\n');
        } else {
            WriteUtf8Wc(file, *wcs);
        }
        wcs++;
    }
}

#define OutputWcs(wcs) WriteUtf8Wcs(file, wcs)
#define OutputWstr(wstr) WriteUtf8Wcsn(file, wstr.wcs, wstr.length)

// Errors:
// 1 - file not readable
// 2 - out of memory
// 3 - syntax error
// 4 - write error
int wmain(int argCount, wchar_t ** args) {
    MkWstr * inputFileContent;
    {
        if (argCount != 2) {
            return 1;
        }
        size_t argLength = wcslen(args[1]);
        if (argLength >= MAX_PATH) {
            return 1;
        }

        HANDLE file = CreateFileW(
            args[1],
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            NULL);
        if (file == INVALID_HANDLE_VALUE) {
            ulong win32Error = GetLastError();
            assert(FALSE);
            if (win32Error == ERROR_FILE_NOT_FOUND) {
                return 1;
            } else {
                return -1;
            }
        }

        LARGE_INTEGER fileSizeStruct;
        if (!GetFileSizeEx(file, &fileSizeStruct)) {
            assert(FALSE);
            return -1;
        }
        ulong fileSize = (ulong)fileSizeStruct.QuadPart;

        // Read Input File

        inputFileContent = MkWstrValAlloc(fileSize);
        if (!inputFileContent) {
            assert(FALSE);
            return 2;
        }
        size_t i = 0;

        const bool smallWc = sizeof(wchar_t) < 4;

        Utf8State state = UTF8_START;
        ulong uc;
        byte input;
        ulong inputCount;
        bool lastWasNewline = FALSE;

        ReadFile(file, &input, 1, &inputCount, NULL);
        while (inputCount != 0 || state == UTF8_END_OK || state == UTF8_END_ERROR) {
            switch (state) {
                case UTF8_START:
                {
                    if ((input & 0b10000000) == 0b00000000) {
                        uc = input;
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_END_OK;
                    } else if ((input & 0b11000000) == 0b10000000) {
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_READ_ERROR;
                    } else if ((input & 0b11100000) == 0b11000000) {
                        uc = (input & 0b00011111) << 6;
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_READ_1;
                    } else if ((input & 0b11110000) == 0b11100000) {
                        uc = (input & 0b00001111) << 12;
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_READ_2;
                    } else if ((input & 0b11111000) == 0b11110000) {
                        uc = (input & 0b00000111) << 18;
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_READ_3;
                    } else {
                        ReadFile(file, &input, 1, &inputCount, NULL);
                        state = UTF8_END_ERROR;
                    }
                    break;
                }

                case UTF8_END_OK:
                {
                    bool success;
                    if (uc > 0xffff && smallWc) {
                        success = AppendWc(&inputFileContent, &i, 0xfffd);
                        lastWasNewline = FALSE;
                    } else if (uc == L'\n' && lastWasNewline) {
                        lastWasNewline = FALSE;
                        success = TRUE;
                    } else if (uc == L'\r') {
                        success = AppendWc(&inputFileContent, &i, L'\n');
                        lastWasNewline = TRUE;
                    } else if (uc == L'\0') {
                        success = TRUE;
                        inputCount = 0;
                        lastWasNewline = FALSE;
                    } else {
                        success = AppendWc(&inputFileContent, &i, (wchar_t)uc);
                        lastWasNewline = FALSE;
                    }

                    if (!success) {
                        assert(FALSE);
                        return 2;
                    }

                    state = UTF8_START;
                    break;
                }

                case UTF8_END_ERROR:
                {
                    if (!AppendWc(&inputFileContent, &i, 0xfffd)) {
                        assert(FALSE);
                        return 2;
                    }
                    state = UTF8_START;
                    break;
                }

                case UTF8_READ_1:
                {
                    if ((input & 0b11000000) == 0b10000000) {
                        uc += input & 0b00111111;
                        state = UTF8_END_OK;
                    } else {
                        state = UTF8_END_ERROR;
                    }
                    ReadFile(file, &input, 1, &inputCount, NULL);
                    break;
                }

                case UTF8_READ_2:
                {
                    if ((input & 0b11000000) == 0b10000000) {
                        uc += (input & 0b00111111) << 6;
                        state = UTF8_READ_1;
                    } else {
                        state = UTF8_END_ERROR;
                    }
                    ReadFile(file, &input, 1, &inputCount, NULL);
                    break;
                }

                case UTF8_READ_3:
                {
                    if ((input & 0b11000000) == 0b10000000) {
                        uc += (input & 0b00111111) << 12;
                        state = UTF8_READ_2;
                    } else {
                        state = UTF8_END_ERROR;
                    }
                    ReadFile(file, &input, 1, &inputCount, NULL);
                    break;
                }

                case UTF8_READ_ERROR:
                {
                    if ((input & 0b11000000) != 0b10000000) {
                        state = UTF8_END_ERROR;
                    }
                    ReadFile(file, &input, 1, &inputCount, NULL);
                    break;
                }

                default:
                {
                    assert(FALSE);
                    return -1;
                }
            }
        }
        if (state != UTF8_START) {
            if (!AppendWc(&inputFileContent, &i, 0xfffd)) {
                assert(FALSE);
                return 2;
            }
        }
        if (!AppendWc(&inputFileContent, &i, L'\0')) {
            assert(FALSE);
            return 2;
        }

        CloseHandle(file);
        inputFileContent->length = i;
    }

    MkList configs;
    ulong inputHeadStart, inputHeadLength;
    {
        inputHeadStart = MkWstrFindSubstrIndexWc(inputFileContent, L"\n");

        inputHeadLength = MkWstrFindSubstrIndexWc(inputFileContent, fullTokenFileBegin);
        if (inputHeadLength == ULONG_MAX) {
            return 3;
        }
        const ulong tokenFileBeginLength = ((sizeof fullTokenFileBegin) / sizeof(wchar_t)) - 1;

        ulong i = inputHeadLength + tokenFileBeginLength;
        if (!(inputFileContent->wcs[i] == L' ' || inputFileContent->wcs[i] == L'\n' || inputFileContent->wcs[i] == L'\t')) {
            return 3;
        }

        MkListInit(&configs, sizeof(Config), 4);
        Config * configPtr = NULL;
        Item * itemPtr = NULL;

        MkWstr validateName;
        MkWstr validateCallback;

        ParseState parseState = PARSE_FILE;
        while (parseState != PARSE_STOP) {
            switch (parseState) {
                case PARSE_FILE:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (!MkWstrIsPrefixWc(inputFileContent, i, tokenPrefix)) {
                        return 3;
                    }
                    i += ((sizeof tokenPrefix) / sizeof(wchar_t)) - 1;

                    if (MkWstrIsPrefixWc(inputFileContent, i, tokenFileEnd)) {
                        parseState = PARSE_STOP;
                    } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenDefBegin)) {
                        parseState = PARSE_DEF_BEGIN_KEYWORD;
                        i += ((sizeof tokenDefBegin) / sizeof(wchar_t)) - 1;
                    } else {
                        return 3;
                    }
                    break;
                }

                case PARSE_DEF_BEGIN_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] == L'(') {
                        parseState = PARSE_DEF_BEGIN_OPEN;
                        i++;

                        configPtr = MkListAppend(&configs, 1);
                        MkListInit(&configPtr->headings, sizeof(Heading), 4);
                        MkListInit(&configPtr->items, sizeof(Item), 16);
                    } else {
                        return 3;
                    }
                    break;
                }

                case PARSE_DEF_BEGIN_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, closeChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&configPtr->name, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_DEF_BEGIN_NAME;
                    break;
                }

                case PARSE_DEF_BEGIN_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }

                case PARSE_DEF:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (!MkWstrIsPrefixWc(inputFileContent, i, tokenPrefix)) {
                        return 3;
                    }
                    i += ((sizeof tokenPrefix) / sizeof(wchar_t)) - 1;

                    if (MkWstrIsPrefixWc(inputFileContent, i, tokenHeading)) {
                        i += ((sizeof tokenHeading) / sizeof(wchar_t)) - 1;
                        parseState = PARSE_HEADING_KEYWORD;
                    } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenPrefixItem)) {
                        i += ((sizeof tokenPrefixItem) / sizeof(wchar_t)) - 1;
                        if (MkWstrIsPrefixWc(inputFileContent, i, tokenItemInt)) {
                            i += ((sizeof tokenItemInt) / sizeof(wchar_t)) - 1;
                            parseState = PARSE_INT_KEYWORD;
                        } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenItemUint)) {
                            i += ((sizeof tokenItemUint) / sizeof(wchar_t)) - 1;
                            parseState = PARSE_UINT_KEYWORD;
                        } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenItemWstr)) {
                            i += ((sizeof tokenItemWstr) / sizeof(wchar_t)) - 1;
                            parseState = PARSE_WSTR_KEYWORD;
                        } else {
                            return 3;
                        }
                    } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenValidate)) {
                        i += ((sizeof tokenValidate) / sizeof(wchar_t)) - 1;
                        parseState = PARSE_VALIDATE_KEYWORD;
                    } else if (MkWstrIsPrefixWc(inputFileContent, i, tokenDefEnd)) {
                        i += ((sizeof tokenDefEnd) / sizeof(wchar_t)) - 1;
                        if (!(inputFileContent->wcs[i] == L' ' || inputFileContent->wcs[i] == L'\t' || inputFileContent->wcs[i] == L'\n')) {
                            return 3;
                        }
                        parseState = PARSE_FILE;
                    } else {
                        return 3;
                    }
                    break;
                }

                case PARSE_HEADING_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L'(') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_HEADING_OPEN;
                    break;
                }

                case PARSE_HEADING_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, closeChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    Heading * headingPtr = MkListAppend(&configPtr->headings, 1);
                    headingPtr->index = configPtr->items.count;
                    MkWstrRefInit(&headingPtr->name, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_HEADING_NAME;
                    break;
                }

                case PARSE_HEADING_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }

                case PARSE_INT_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L'(') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_INT_OPEN;
                    break;
                }

                case PARSE_INT_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, sepChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    itemPtr = MkListAppend(&configPtr->items, 1);
                    itemPtr->type = ITEM_INT;
                    MkWstrRefInit(&itemPtr->name, j - i, inputFileContent->wcs + i);
                    itemPtr->validateCallback.length = 0;
                    i = j;
                    parseState = PARSE_INT_NAME;
                    break;
                }

                case PARSE_INT_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L',') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_INT_SEP;
                    break;
                }

                case PARSE_INT_SEP:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, closeChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&itemPtr->defaultValue, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_INT_DEFAULT;
                    break;
                }

                case PARSE_INT_DEFAULT:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }

                case PARSE_UINT_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L'(') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_UINT_OPEN;
                    break;
                }

                case PARSE_UINT_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, sepChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    itemPtr = MkListAppend(&configPtr->items, 1);
                    itemPtr->type = ITEM_UINT;
                    MkWstrRefInit(&itemPtr->name, j - i, inputFileContent->wcs + i);
                    itemPtr->validateCallback.length = 0;
                    i = j;
                    parseState = PARSE_UINT_NAME;
                    break;
                }

                case PARSE_UINT_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L',') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_UINT_SEP;
                    break;
                }

                case PARSE_UINT_SEP:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, closeChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&itemPtr->defaultValue, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_UINT_DEFAULT;
                    break;
                }

                case PARSE_UINT_DEFAULT:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }

                case PARSE_WSTR_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L'(') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_WSTR_OPEN;
                    break;
                }

                case PARSE_WSTR_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, sepChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    itemPtr = MkListAppend(&configPtr->items, 1);
                    itemPtr->type = ITEM_WSTR;
                    MkWstrRefInit(&itemPtr->name, j - i, inputFileContent->wcs + i);
                    itemPtr->validateCallback.length = 0;
                    i = j;
                    parseState = PARSE_WSTR_NAME;
                    break;
                }

                case PARSE_WSTR_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L',') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_WSTR_NAME_SEP;
                    break;
                }

                case PARSE_WSTR_NAME_SEP:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, sepChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&itemPtr->length, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_WSTR_COUNT;
                    break;
                }

                case PARSE_WSTR_COUNT:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L',') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_WSTR_COUNT_SEP;
                    break;
                }

                case PARSE_WSTR_COUNT_SEP:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (!(inputFileContent->wcs[i] == L'L' && inputFileContent->wcs[i + 1] == L'\"')) {
                        return 3;
                    }
                    i += 2;
                    ulong j = i;
                    while (!(inputFileContent->wcs[j] == L'\"' && (j == i || inputFileContent->wcs[j - 1] != L'\\'))) {
                        j++;
                    }
                    MkWstrRefInit(&itemPtr->defaultValue, j - i, inputFileContent->wcs + i);
                    i = j + 1;
                    parseState = PARSE_WSTR_DEFAULT;
                    break;
                }

                case PARSE_WSTR_DEFAULT:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }

                case PARSE_VALIDATE_KEYWORD:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L'(') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_VALIDATE_OPEN;
                    break;
                }

                case PARSE_VALIDATE_OPEN:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, sepChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&validateName, j - i, inputFileContent->wcs + i);
                    i = j;
                    parseState = PARSE_VALIDATE_NAME;
                    break;
                }

                case PARSE_VALIDATE_NAME:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L',') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_VALIDATE_SEP;
                    break;
                }

                case PARSE_VALIDATE_SEP:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    ulong j = MkWstrFindCharsNextIndex(inputFileContent, i, closeChars, 4);
                    if (j == ULONG_MAX) {
                        return 3;
                    }
                    MkWstrRefInit(&validateCallback, j - i, inputFileContent->wcs + i);

                    for (int k = 0; k != configPtr->items.count; k++) {
                        Item * validateItemPtr = MkListGet(&configPtr->items, k);
                        if (MkWstrsAreEqual(&validateItemPtr->name, &validateName)) {
                            validateItemPtr->validateCallback = validateCallback;
                        }
                    }

                    i = j;
                    parseState = PARSE_VALIDATE_CALLBACK;
                    break;
                }

                case PARSE_VALIDATE_CALLBACK:
                {
                    ConsumeWhitespace(inputFileContent->wcs, i);
                    if (inputFileContent->wcs[i] != L')') {
                        return 3;
                    }
                    i++;
                    parseState = PARSE_DEF;
                    break;
                }
            }
        }
    }

    const wchar_t * fileName;
    ulong fileBaseNameLength;
    {
        fileName = wcsrchr(args[1], L'\\');
        if (fileName) {
            fileName++;
        } else {
            fileName = args[1];
        }
        const wchar_t * extBegin = wcsrchr(fileName, L'.');
        if (extBegin) {
            fileBaseNameLength = extBegin - fileName;
        } else {
            fileBaseNameLength = wcslen(fileName);
        }
    }

    wchar_t headerFileName[MAX_PATH];
    {
        wcsncpy_s(headerFileName, MAX_PATH, fileName, fileBaseNameLength);
        wcscat_s(headerFileName, MAX_PATH, L"Gen.h");

        wchar_t headerFilePath[MAX_PATH];
        wcsncpy_s(headerFilePath, MAX_PATH, args[1], fileName - args[1]);
        wcscat_s(headerFilePath, MAX_PATH, headerFileName);

        HANDLE file = CreateFileW(
            headerFilePath,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (file == INVALID_HANDLE_VALUE) {
            return 4;
        }

        OutputWcs(L"//---------------------//\n");
        OutputWcs(L"// AUTO-GENERATED FILE //\n");
        OutputWcs(L"//---------------------//\n\n");

        wchar_t fileBaseNameUpper[MAX_PATH];
        for (ulong i = 0; i != fileBaseNameLength; i++) {
            fileBaseNameUpper[i] = towupper(fileName[i]);
        }
        fileBaseNameUpper[fileBaseNameLength] = L'\0';

        OutputWcs(L"#ifndef _MKCONFGEN_");
        OutputWcs(fileBaseNameUpper);
        OutputWcs(L"_H\n");

        OutputWcs(L"#define _MKCONFGEN_");
        OutputWcs(fileBaseNameUpper);
        OutputWcs(L"_H\n");

        OutputWcs(L"\n#include <wchar.h>\n");
        WriteUtf8Wcsn(file, inputFileContent->wcs, inputHeadStart);

        for (ulong i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\nstruct ");
            OutputWstr(configPtr->name);
            OutputWcs(L" {");

            ulong headingIndex = 0;
            Heading * headingPtr;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = MkListGet(&configPtr->headings, headingIndex);
            } else {
                headingPtr = NULL;
            }

            for (ulong j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n    // ");
                    OutputWstr(headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = MkListGet(&configPtr->headings, headingIndex);
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = MkListGet(&configPtr->items, j);
                OutputWcs(L"\n    ");

                switch (itemPtr->type) {
                    case ITEM_INT:
                    {
                        OutputWcs(L"long ");
                        OutputWstr(itemPtr->name);
                        break;
                    }

                    case ITEM_UINT:
                    {
                        OutputWcs(L"unsigned long ");
                        OutputWstr(itemPtr->name);
                        break;
                    }

                    case ITEM_WSTR:
                    {
                        OutputWcs(L"wchar_t ");
                        OutputWstr(itemPtr->name);
                        OutputWcs(L"[");
                        OutputWstr(itemPtr->length);
                        OutputWcs(L"]");
                        break;
                    }
                }
                OutputWcs(L";");
            }

            OutputWcs(L"\n};");
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//---------------");
        OutputWcs(L"\n// Default Values");

        for (ulong i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\n// ");
            OutputWstr(configPtr->name);

            Heading * headingPtr;
            ulong headingIndex = 0;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = MkListGet(&configPtr->headings, headingIndex);
            } else {
                headingPtr = NULL;
            }

            for (ulong j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n// ");
                    OutputWstr(headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = MkListGet(&configPtr->headings, headingIndex);
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = MkListGet(&configPtr->items, j);

                OutputWcs(L"\nextern const ");
                switch (itemPtr->type) {
                    case ITEM_INT:
                        OutputWcs(L"long ");
                        break;

                    case ITEM_UINT:
                        OutputWcs(L"unsigned long ");
                        break;

                    case ITEM_WSTR:
                        OutputWcs(L"wchar_t ");
                        break;
                }

                OutputWstr(configPtr->name);
                OutputWcs(L"Default_");
                OutputWstr(itemPtr->name);

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"[");
                    OutputWstr(itemPtr->length);
                    OutputWcs(L"]");
                }

                OutputWcs(L";");
            }
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//----------");
        OutputWcs(L"\n// Functions");

        for (int i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\nvoid ");
            OutputWstr(configPtr->name);
            OutputWcs(L"Init(");
            OutputWstr(configPtr->name);
            OutputWcs(L" * configPtr);");

            OutputWcs(L"\n\nvoid ");
            OutputWstr(configPtr->name);
            OutputWcs(L"Load(");

            OutputWcs(L"\n    ");
            OutputWstr(configPtr->name);
            OutputWcs(L" * configPtr,");

            OutputWcs(L"\n    void * stream,");
            OutputWcs(L"\n    MkConfGenStreamNextCallback nextCallback,");
            OutputWcs(L"\n    void ** stopStatus,");
            OutputWcs(L"\n    MkConfGenLoadError ** errors,");
            OutputWcs(L"\n    unsigned long * errorCount);");
        }

        OutputWcs(L"\n\n#endif");

        CloseHandle(file);
    }

    {
        wchar_t implFilePath[MAX_PATH];
        wcsncpy_s(implFilePath, MAX_PATH, args[1], fileName - args[1]);
        wcsncat_s(implFilePath, MAX_PATH, fileName, fileBaseNameLength);
        wcscat_s(implFilePath, MAX_PATH, L"Gen.cpp");

        HANDLE file = CreateFileW(
            implFilePath,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (file == INVALID_HANDLE_VALUE) {
            return 4;
        }

        OutputWcs(L"//---------------------//\n");
        OutputWcs(L"// AUTO-GENERATED FILE //\n");
        OutputWcs(L"//---------------------//\n\n");

        OutputWcs(L"#include \"");
        OutputWcs(headerFileName);
        OutputWcs(L"\"");

        OutputWcs(L"\n");
        OutputWcs(L"\n//-----------------------");
        OutputWcs(L"\n// Copied Input File Head");
        
        OutputWcs(L"\n\n");
        WriteUtf8Wcsn(file, inputFileContent->wcs + inputHeadStart, inputHeadLength - inputHeadStart);

        OutputWcs(L"\n");
        OutputWcs(L"\n//-----");
        OutputWcs(L"\n// Keys");

        for (int i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\nconst unsigned long _mkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"Indices[] = {");

            ulong currentKeyIndex = 0;
            wchar_t tmpBuffer[32];
            for (int j = 0; j != configPtr->items.count; j++) {
                swprintf_s(tmpBuffer, 32, L"\n    %lu,", currentKeyIndex);
                OutputWcs(tmpBuffer);

                Item * itemPtr = MkListGet(&configPtr->items, j);
                currentKeyIndex += itemPtr->name.length;
            }
            swprintf_s(tmpBuffer, 32, L"\n    %lu,", currentKeyIndex);
            OutputWcs(tmpBuffer);
            OutputWcs(L"\n};");

            OutputWcs(L"\n\nconst wchar_t _mkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"Keys[] =");

            for (int j = 0; j != configPtr->items.count; j++) {
                Item * itemPtr = MkListGet(&configPtr->items, j);
                OutputWcs(L"\n    L\"");
                OutputWstr(itemPtr->name);
                OutputWcs(L"\"");
            }
            OutputWcs(L";");
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//---------------");
        OutputWcs(L"\n// Default Values");

        for (ulong i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\n// ");
            OutputWstr(configPtr->name);

            Heading * headingPtr;
            ulong headingIndex = 0;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = MkListGet(&configPtr->headings, headingIndex);
            } else {
                headingPtr = NULL;
            }

            for (ulong j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n// ");
                    OutputWstr(headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = MkListGet(&configPtr->headings, headingIndex);
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = MkListGet(&configPtr->items, j);

                OutputWcs(L"\nconst ");
                switch (itemPtr->type) {
                    case ITEM_INT:
                        OutputWcs(L"long ");
                        break;

                    case ITEM_UINT:
                        OutputWcs(L"unsigned long ");
                        break;

                    case ITEM_WSTR:
                        OutputWcs(L"wchar_t ");
                        break;
                }

                OutputWstr(configPtr->name);
                OutputWcs(L"Default_");
                OutputWstr(itemPtr->name);

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"[");
                    OutputWstr(itemPtr->length);
                    OutputWcs(L"] = L\"");
                    OutputWstr(itemPtr->defaultValue);
                    OutputWcs(L"\";");
                } else {
                    OutputWcs(L" = ");
                    OutputWstr(itemPtr->defaultValue);
                    OutputWcs(L";");
                }
            }
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//----------");
        OutputWcs(L"\n// Functions");

        for (int i = 0; i != configs.count; i++) {
            Config * configPtr = MkListGet(&configs, i);

            OutputWcs(L"\n\nvoid ");
            OutputWstr(configPtr->name);
            OutputWcs(L"Init(");
            OutputWstr(configPtr->name);
            OutputWcs(L" * configPtr) {");
            OutputWcs(L"\n    _MKCONFGEN_ASSERT(configPtr);");

            ulong headingIndex = 0;
            Heading * headingPtr;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = MkListGet(&configPtr->headings, headingIndex);
            } else {
                headingPtr = NULL;
            }

            for (int j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n    // ");
                    OutputWstr(headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = MkListGet(&configPtr->headings, headingIndex);
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = MkListGet(&configPtr->items, j);

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"\n    wcscpy_s(configPtr->");
                    OutputWstr(itemPtr->name);
                    OutputWcs(L", ");
                    OutputWstr(itemPtr->length);
                    OutputWcs(L", ");
                    OutputWstr(configPtr->name);
                    OutputWcs(L"Default_");
                    OutputWstr(itemPtr->name);
                    OutputWcs(L");");
                } else {
                    OutputWcs(L"\n    configPtr->");
                    OutputWstr(itemPtr->name);
                    OutputWcs(L" = ");
                    OutputWstr(configPtr->name);
                    OutputWcs(L"Default_");
                    OutputWstr(itemPtr->name);
                    OutputWcs(L";");
                }
            }

            OutputWcs(L"\n}");

            OutputWcs(L"\n\nstatic bool _MkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"ParseValue(");
            OutputWcs(L"\n    void * config,");
            OutputWcs(L"\n    unsigned long index,");
            OutputWcs(L"\n    wchar_t * rawValue,");
            OutputWcs(L"\n    unsigned long rawValueLength,");
            OutputWcs(L"\n    bool isStr,");
            OutputWcs(L"\n    MkConfGenLoadErrorType * errorType)");
            OutputWcs(L"\n{");

            OutputWcs(L"\n    ");
            OutputWstr(configPtr->name);
            OutputWcs(L" * configPtr = (");
            OutputWstr(configPtr->name);
            OutputWcs(L" *)config;");

            OutputWcs(L"\n    switch (index) {");

            for (int j = 0; j != configPtr->items.count; j++) {
                Item * itemPtr = MkListGet(&configPtr->items, j);

                wchar_t tmpBuffer[64];
                swprintf_s(tmpBuffer, 64, L"\n\n        case %lu:", j);
                OutputWcs(tmpBuffer);
                OutputWcs(L"\n        {");

                switch (itemPtr->type) {
                    case ITEM_INT:
                    {
                        OutputWcs(L"\n            if (isStr) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n");
                        OutputWcs(L"\n            wchar_t * end;");
                        OutputWcs(L"\n            long value = wcstol(rawValue, &end, 0);");
                        OutputWcs(L"\n            if (end != rawValue + rawValueLength) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n            if (value == LONG_MIN || value == LONG_MAX) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        if (itemPtr->validateCallback.length != 0) {
                            OutputWcs(L"\n            if (!");
                            OutputWstr(itemPtr->validateCallback);
                            OutputWcs(L"(value)) {");
                            OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_INVALID;");
                            OutputWcs(L"\n                return false;");
                            OutputWcs(L"\n            }");
                        }

                        OutputWcs(L"\n            configPtr->");
                        OutputWstr(itemPtr->name);
                        OutputWcs(L" = value;");

                        OutputWcs(L"\n            return true;");
                        OutputWcs(L"\n        }");
                        break;
                    }

                    case ITEM_UINT:
                    {
                        OutputWcs(L"\n            if (isStr) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n");
                        OutputWcs(L"\n            wchar_t * end;");
                        OutputWcs(L"\n            unsigned long value = wcstoul(rawValue, &end, 0);");
                        OutputWcs(L"\n            if (end != rawValue + rawValueLength) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n            if (value == ULONG_MAX) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        if (itemPtr->validateCallback.length != 0) {
                            OutputWcs(L"\n            if (!");
                            OutputWstr(itemPtr->validateCallback);
                            OutputWcs(L"(value)) {");
                            OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_INVALID;");
                            OutputWcs(L"\n                return false;");
                            OutputWcs(L"\n            }");
                        }

                        OutputWcs(L"\n            configPtr->");
                        OutputWstr(itemPtr->name);
                        OutputWcs(L" = value;");

                        OutputWcs(L"\n            return true;");
                        OutputWcs(L"\n        }");
                        break;
                    }

                    case ITEM_WSTR:
                    {
                        OutputWcs(L"\n            if (!isStr) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        OutputWcs(L"\n            if (rawValueLength > ");
                        OutputWstr(itemPtr->length);
                        OutputWcs(L") { ");

                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        OutputWcs(L"\n            wcscpy_s(configPtr->");
                        OutputWstr(itemPtr->name);
                        OutputWcs(L", ");
                        OutputWstr(itemPtr->length);
                        OutputWcs(L", rawValue);");

                        OutputWcs(L"\n            return true;");
                        OutputWcs(L"\n        }");
                        break;
                    }
                }
            }

            OutputWcs(L"\n");
            OutputWcs(L"\n        default:");
            OutputWcs(L"\n        {");
            OutputWcs(L"\n            *errorType = MKCONFGEN_LOAD_ERROR_UNDEFINED;");
            OutputWcs(L"\n            return false;");
            OutputWcs(L"\n        }");

            OutputWcs(L"\n    }");
            OutputWcs(L"\n}");

            OutputWcs(L"\n\nvoid ");
            OutputWstr(configPtr->name);
            OutputWcs(L"Load(");

            OutputWcs(L"\n    ");
            OutputWstr(configPtr->name);
            OutputWcs(L" * configPtr,");

            OutputWcs(L"\n    void * stream,");
            OutputWcs(L"\n    MkConfGenStreamNextCallback nextCallback,");
            OutputWcs(L"\n    void ** stopStatus,");
            OutputWcs(L"\n    MkConfGenLoadError ** errors,");
            OutputWcs(L"\n    unsigned long * errorCount)");
            OutputWcs(L"\n{");
            OutputWcs(L"\n    _MkConfGenLoad(");
            OutputWcs(L"\n        nextCallback,");
            OutputWcs(L"\n        stream,");
            OutputWcs(L"\n        stopStatus,");

            wchar_t tmpBuffer[32];
            swprintf_s(tmpBuffer, 32, L"\n        %lu,", configPtr->items.count);
            OutputWcs(tmpBuffer);

            OutputWcs(L"\n        _mkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"Indices,");

            OutputWcs(L"\n        _mkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"Keys,");

            OutputWcs(L"\n        _MkConfGen");
            OutputWstr(configPtr->name);
            OutputWcs(L"ParseValue,");


            OutputWcs(L"\n        configPtr,");
            OutputWcs(L"\n        errors,");
            OutputWcs(L"\n        errorCount);");

            OutputWcs(L"\n}");
        }

        CloseHandle(file);
    }

    for (int i = 0; i != configs.count; i++) {
        Config * configPtr = MkListGet(&configs, i);

        wchar_t filePath[MAX_PATH];
        wcsncpy_s(filePath, MAX_PATH, args[1], fileName - args[1]);
        wcsncat_s(filePath, MAX_PATH, configPtr->name.wcs, configPtr->name.length);
        wcscat_s(filePath, MAX_PATH, L"_example.cfg");

        HANDLE file = CreateFileW(
            filePath,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (file == INVALID_HANDLE_VALUE) {
            return 4;
        }

        ulong headingIndex = 0;
        Heading * headingPtr;
        if (headingIndex != configPtr->headings.count) {
            headingPtr = MkListGet(&configPtr->headings, headingIndex);
            if (headingPtr->index == 0) {
                OutputWcs(L"// ");
                OutputWstr(headingPtr->name);

                if (++headingIndex != configPtr->headings.count) {
                    headingPtr = MkListGet(&configPtr->headings, headingIndex);
                } else {
                    headingPtr = NULL;
                }
            }
        } else {
            headingPtr = NULL;
        }

        for (int j = 0; j != configPtr->items.count; j++) {
            if (headingPtr != NULL && headingPtr->index == j) {
                OutputWcs(L"\n\n// ");
                OutputWstr(headingPtr->name);

                if (++headingIndex != configPtr->headings.count) {
                    headingPtr = MkListGet(&configPtr->headings, headingIndex);
                } else {
                    headingPtr = NULL;
                }
            }

            Item * itemPtr = MkListGet(&configPtr->items, j);
            OutputWcs(L"\n");
            OutputWstr(itemPtr->name);
            OutputWcs(L" = ");
            if (itemPtr->type == ITEM_WSTR) {
                OutputWcs(L"\"");
            }
            OutputWstr(itemPtr->defaultValue);
            if (itemPtr->type == ITEM_WSTR) {
                OutputWcs(L"\"");
            }
        }

        CloseHandle(file);
    }

    return 0;
}