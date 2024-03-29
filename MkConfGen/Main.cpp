#include <Windows.h>

#include <assert.h>
#include <wchar.h>

#include "Import/MkDynArray.h"
#include "Import/MkString.h"

// 0 - ok
// 1 - file not readable
// 2 - out of memory
int ReadInputFile(wchar_t * filePath, MkDynArray<wchar_t> * inputListPtr) {
    size_t argLength = wcslen(filePath);
    if (argLength >= MAX_PATH) {
        return 1;
    }

    HANDLE file = CreateFileW(
        filePath,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 1;
    }

    auto readCallback = [](void * stream, void * buffer, ulong count, void * status) {
        ulong * error = static_cast<ulong *>(status);

        ulong readCount;
        if (ReadFile(stream, buffer, count, &readCount, nullptr)) {
            *error = ERROR_SUCCESS;
            if (readCount == 0) {
                return false;
            } else {
                return true;
            }
        } else {
            *error = GetLastError();
            return false;
        }
    };

    auto writeCallback = [](void * stream, const void * buffer, ulong count, void * status) {
        MkDynArray<wchar_t> * list = (MkDynArray<wchar_t> *)stream;
        const wchar_t * chars = (const wchar_t *)buffer;

        wchar_t * newElems = list->Insert(SIZE_MAX, count);
        if (!newElems) {
            return false;
        }
        for (ulong i = 0; i != count; i++) {
            newElems[i] = chars[i];
        }
        return true;
    };

    ulong readStatus;
    bool success = MkUtf8Read(
        readCallback, file, &readStatus,
        writeCallback, inputListPtr, nullptr);

    CloseHandle(file);
    if (!success) {
        return 2;
    }

    return 0;
}

struct Heading {
    size_t index;
    MkWstr name;
};

enum ItemType {
    ITEM_NONE,
    ITEM_INT,
    ITEM_UINT,
    ITEM_FLOAT,
    ITEM_WSTR,
};

struct Item {
    ItemType type;
    MkWstr name;
    MkWstr length; // WSTR only
    MkWstr defaultValue;
    MkWstr validateCallback;
};

struct Config {
    MkWstr name;
    MkDynArray<Heading> headings;
    MkDynArray<Item> items;
};

enum ParseState {
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
    PARSE_FLOAT_KEYWORD,
    PARSE_FLOAT_OPEN,
    PARSE_FLOAT_NAME,
    PARSE_FLOAT_SEP,
    PARSE_FLOAT_DEFAULT,
    PARSE_VALIDATE_KEYWORD,
    PARSE_VALIDATE_OPEN,
    PARSE_VALIDATE_NAME,
    PARSE_VALIDATE_SEP,
    PARSE_VALIDATE_CALLBACK,
    PARSE_STOP,
};

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
const wchar_t tokenItemFloat[] = L"FLOAT";

const wchar_t closeChars[] = { L' ', L'\t', L'\n', L')' };
const wchar_t sepChars[] = { L' ', L'\t', L'\n', L',' };

#define ConsumeWhitespace() while (inputWcs < inputWcsEnd && (*inputWcs == L' ' || *inputWcs == L'\t' || *inputWcs == L'\n')) inputWcs++
#define CheckTruncation() if (inputWcs >= inputWcsEnd) return 3
#define AdvanceAndCheck(n) inputWcs += (n); CheckTruncation()
#define WcsLengthR(s) ((sizeof s / sizeof(wchar_t)) - 1)
#define inputWcsLength (inputWcsEnd - inputWcs)

// 0 - ok
// 3 - syntax error
int Parse(MkDynArray<wchar_t> * inputWcsListPtr, MkDynArray<Config> * configsPtr, MkWstr * includeLinePtr, MkWstr * inputHeadPtr) {
    wchar_t * inputWcs = inputWcsListPtr->elems;
    wchar_t * inputWcsEnd = inputWcs + inputWcsListPtr->count;

    // Include Line

    includeLinePtr->wcs = inputWcs;
    includeLinePtr->length = MkWcsFindCharIndex(inputWcs, inputWcsLength, L'\n');
    if (includeLinePtr->length == SIZE_MAX) {
        return 3;
    }
    AdvanceAndCheck(includeLinePtr->length + 1);

    // User Code

    inputHeadPtr->wcs = inputWcs;
    inputHeadPtr->length = MkWcsFindSubstrIndex(inputWcs, inputWcsLength, fullTokenFileBegin);
    if (inputHeadPtr->length == SIZE_MAX) {
        return 3;
    }
    AdvanceAndCheck(inputHeadPtr->length + WcsLengthR(fullTokenFileBegin));

    if (!(*inputWcs == L' ' || *inputWcs == L'\n' || *inputWcs == L'\t')) {
        return 3;
    }

    // Definitions

    Config * configPtr = NULL;
    Item * itemPtr = NULL;

    MkWstr validateName;
    MkWstr validateCallback;

    ParseState parseState = PARSE_FILE;
    while (parseState != PARSE_STOP) {
        switch (parseState) {
            case PARSE_FILE:
            {
                ConsumeWhitespace();
                if (!MkWcsIsPrefix(inputWcs, inputWcsLength, tokenPrefix)) {
                    return 3;
                }
                AdvanceAndCheck(WcsLengthR(tokenPrefix));

                if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenFileEnd)) {
                    parseState = PARSE_STOP;
                } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenDefBegin)) {
                    AdvanceAndCheck((WcsLengthR(tokenDefBegin)));
                    parseState = PARSE_DEF_BEGIN_KEYWORD;
                } else {
                    return 3;
                }
                break;
            }

            case PARSE_DEF_BEGIN_KEYWORD:
            {
                ConsumeWhitespace();
                CheckTruncation();

                if (*inputWcs == L'(') {
                    AdvanceAndCheck(1);
                    parseState = PARSE_DEF_BEGIN_OPEN;

                    configPtr = configsPtr->Insert(SIZE_MAX, 1);
                    configPtr->headings.Init(4);
                    configPtr->items.Init(16);
                } else {
                    return 3;
                }
                break;
            }

            case PARSE_DEF_BEGIN_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&configPtr->name, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_DEF_BEGIN_NAME;
                break;
            }

            case PARSE_DEF_BEGIN_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_DEF:
            {
                ConsumeWhitespace();
                if (!MkWcsIsPrefix(inputWcs, inputWcsLength, tokenPrefix)) {
                    return 3;
                }
                AdvanceAndCheck(WcsLengthR(tokenPrefix));

                if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenHeading)) {
                    AdvanceAndCheck(WcsLengthR(tokenHeading));
                    parseState = PARSE_HEADING_KEYWORD;
                } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenPrefixItem)) {
                    AdvanceAndCheck(WcsLengthR(tokenPrefixItem));

                    if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenItemInt)) {
                        AdvanceAndCheck(WcsLengthR(tokenItemInt));
                        parseState = PARSE_INT_KEYWORD;
                    } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenItemUint)) {
                        AdvanceAndCheck(WcsLengthR(tokenItemUint));
                        parseState = PARSE_UINT_KEYWORD;
                    } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenItemFloat)) {
                        AdvanceAndCheck(WcsLengthR(tokenItemFloat));
                        parseState = PARSE_FLOAT_KEYWORD;
                    } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenItemWstr)) {
                        AdvanceAndCheck(WcsLengthR(tokenItemWstr));
                        parseState = PARSE_WSTR_KEYWORD;
                    } else {
                        return 3;
                    }
                } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenValidate)) {
                    AdvanceAndCheck(WcsLengthR(tokenValidate));
                    parseState = PARSE_VALIDATE_KEYWORD;
                } else if (MkWcsIsPrefix(inputWcs, inputWcsLength, tokenDefEnd)) {
                    AdvanceAndCheck(WcsLengthR(tokenDefEnd));
                    if (!(*inputWcs == L' ' || *inputWcs == L'\t' || *inputWcs == L'\n')) {
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
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_HEADING_OPEN;
                break;
            }

            case PARSE_HEADING_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                Heading * headingPtr = configPtr->headings.Insert(SIZE_MAX, 1);
                headingPtr->index = configPtr->items.count;
                MkWstrSet(&headingPtr->name, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_HEADING_NAME;
                break;
            }

            case PARSE_HEADING_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_INT_KEYWORD:
            {
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_INT_OPEN;
                break;
            }

            case PARSE_INT_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                itemPtr = configPtr->items.Insert(SIZE_MAX, 1);
                itemPtr->type = ITEM_INT;
                MkWstrSet(&itemPtr->name, inputWcs, j);
                itemPtr->validateCallback.length = 0;
                AdvanceAndCheck(j);
                parseState = PARSE_INT_NAME;
                break;
            }

            case PARSE_INT_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_INT_SEP;
                break;
            }

            case PARSE_INT_SEP:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&itemPtr->defaultValue, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_INT_DEFAULT;
                break;
            }

            case PARSE_INT_DEFAULT:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_UINT_KEYWORD:
            {
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_UINT_OPEN;
                break;
            }

            case PARSE_UINT_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                itemPtr = configPtr->items.Insert(SIZE_MAX, 1);
                itemPtr->type = ITEM_UINT;
                MkWstrSet(&itemPtr->name, inputWcs, j);
                itemPtr->validateCallback.length = 0;
                AdvanceAndCheck(j);
                parseState = PARSE_UINT_NAME;
                break;
            }

            case PARSE_UINT_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_UINT_SEP;
                break;
            }

            case PARSE_UINT_SEP:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&itemPtr->defaultValue, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_UINT_DEFAULT;
                break;
            }

            case PARSE_UINT_DEFAULT:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_FLOAT_KEYWORD:
            {
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_FLOAT_OPEN;
                break;
            }

            case PARSE_FLOAT_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                itemPtr = configPtr->items.Insert(SIZE_MAX, 1);
                itemPtr->type = ITEM_FLOAT;
                MkWstrSet(&itemPtr->name, inputWcs, j);
                itemPtr->validateCallback.length = 0;
                AdvanceAndCheck(j);
                parseState = PARSE_FLOAT_NAME;
                break;
            }

            case PARSE_FLOAT_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_FLOAT_SEP;
                break;
            }

            case PARSE_FLOAT_SEP:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&itemPtr->defaultValue, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_FLOAT_DEFAULT;
                break;
            }

            case PARSE_FLOAT_DEFAULT:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_WSTR_KEYWORD:
            {
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_WSTR_OPEN;
                break;
            }

            case PARSE_WSTR_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                itemPtr = configPtr->items.Insert(SIZE_MAX, 1);
                itemPtr->type = ITEM_WSTR;
                MkWstrSet(&itemPtr->name, inputWcs, j);
                itemPtr->validateCallback.length = 0;
                AdvanceAndCheck(j);
                parseState = PARSE_WSTR_NAME;
                break;
            }

            case PARSE_WSTR_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_WSTR_NAME_SEP;
                break;
            }

            case PARSE_WSTR_NAME_SEP:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&itemPtr->length, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_WSTR_COUNT;
                break;
            }

            case PARSE_WSTR_COUNT:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_WSTR_COUNT_SEP;
                break;
            }

            case PARSE_WSTR_COUNT_SEP:
            {
                ConsumeWhitespace();

                if (!(inputWcsLength >= 2 && *inputWcs == L'L' && *(inputWcs + 1) == L'\"')) {
                    return 3;
                }
                AdvanceAndCheck(2);

                size_t j = 0;
                while (!(inputWcs[j] == L'\"' && (j == 0 || inputWcs[j - 1] != L'\\'))) {
                    j++;
                }
                MkWstrSet(&itemPtr->defaultValue, inputWcs, j);
                AdvanceAndCheck(j + 1);

                parseState = PARSE_WSTR_DEFAULT;
                break;
            }

            case PARSE_WSTR_DEFAULT:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }

            case PARSE_VALIDATE_KEYWORD:
            {
                ConsumeWhitespace();
                if (*inputWcs != L'(') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_VALIDATE_OPEN;
                break;
            }

            case PARSE_VALIDATE_OPEN:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, sepChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&validateName, inputWcs, j);
                AdvanceAndCheck(j);
                parseState = PARSE_VALIDATE_NAME;
                break;
            }

            case PARSE_VALIDATE_NAME:
            {
                ConsumeWhitespace();
                if (*inputWcs != L',') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_VALIDATE_SEP;
                break;
            }

            case PARSE_VALIDATE_SEP:
            {
                ConsumeWhitespace();
                size_t j = MkWcsFindCharsIndex(inputWcs, inputWcsLength, closeChars, 4);
                if (j == SIZE_MAX) {
                    return 3;
                }
                MkWstrSet(&validateCallback, inputWcs, j);

                for (int k = 0; k != configPtr->items.count; k++) {
                    Item * validateItemPtr = &configPtr->items.elems[k];
                    if (MkWcsAreEqual(validateItemPtr->name.wcs, validateItemPtr->name.length, validateName.wcs, validateName.length)) {
                        validateItemPtr->validateCallback = validateCallback;
                    }
                }

                AdvanceAndCheck(j);
                parseState = PARSE_VALIDATE_CALLBACK;
                break;
            }

            case PARSE_VALIDATE_CALLBACK:
            {
                ConsumeWhitespace();
                if (*inputWcs != L')') {
                    return 3;
                }
                AdvanceAndCheck(1);
                parseState = PARSE_DEF;
                break;
            }
        }
    }

    return 0;
}

#define OutputWcs(s) if (!MkUtf8WriteWcs((s), SIZE_MAX, true, writeCallback, file, nullptr)) return 4
#define OutputWstr(s) if (!MkUtf8WriteWcs((s)->wcs, (s)->length, true, writeCallback, file, nullptr)) return 4

// Errors:
// 1 - file not readable
// 2 - out of memory
// 3 - syntax error
// 4 - write error
int wmain(int argCount, wchar_t ** args) {
    if (argCount != 2) {
        return 1;
    }

    int rc;

    MkDynArray<wchar_t> inputWcsList;
    inputWcsList.Init(128);
    rc = ReadInputFile(args[1], &inputWcsList);
    if (rc != 0) return rc;

    MkDynArray<Config> configs;
    configs.Init(4);
    MkWstr includeLine;
    MkWstr inputHead;
    rc = Parse(&inputWcsList, &configs, &includeLine, &inputHead);

    const wchar_t * fileName;
    size_t fileBaseNameLength;
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

    auto writeCallback = [](void * stream, const void * buffer, ulong count, void * status) {
        ulong writeCount;
        return (bool)WriteFile(stream, buffer, count, &writeCount, nullptr);
    };

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
        for (size_t i = 0; i != fileBaseNameLength; i++) {
            fileBaseNameUpper[i] = towupper(fileName[i]);
        }
        fileBaseNameUpper[fileBaseNameLength] = L'\0';

        OutputWcs(L"#ifndef _MKCONFGEN_");
        OutputWcs(fileBaseNameUpper);
        OutputWcs(L"_H\n");

        OutputWcs(L"#define _MKCONFGEN_");
        OutputWcs(fileBaseNameUpper);
        OutputWcs(L"_H\n");

        OutputWcs(L"\n#include <math.h>");
        OutputWcs(L"\n#include <stddef.h>");
        OutputWcs(L"\n#include <wchar.h>\n");
        OutputWstr(&includeLine);

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\nstruct ");
            OutputWstr(&configPtr->name);
            OutputWcs(L" {");

            size_t headingIndex = 0;
            Heading * headingPtr;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = &configPtr->headings.elems[headingIndex];
            } else {
                headingPtr = NULL;
            }

            for (size_t j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n    // ");
                    OutputWstr(&headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = &configPtr->headings.elems[headingIndex];
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = &configPtr->items.elems[j];
                OutputWcs(L"\n    ");

                switch (itemPtr->type) {
                    case ITEM_INT:
                    {
                        OutputWcs(L"long ");
                        OutputWstr(&itemPtr->name);
                        break;
                    }

                    case ITEM_UINT:
                    {
                        OutputWcs(L"unsigned long ");
                        OutputWstr(&itemPtr->name);
                        break;
                    }

                    case ITEM_FLOAT:
                    {
                        OutputWcs(L"double ");
                        OutputWstr(&itemPtr->name);
                        break;
                    }

                    case ITEM_WSTR:
                    {
                        OutputWcs(L"wchar_t ");
                        OutputWstr(&itemPtr->name);
                        OutputWcs(L"[");
                        OutputWstr(&itemPtr->length);
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

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\n// ");
            OutputWstr(&configPtr->name);

            Heading * headingPtr;
            size_t headingIndex = 0;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = &configPtr->headings.elems[headingIndex];
            } else {
                headingPtr = NULL;
            }

            for (size_t j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n// ");
                    OutputWstr(&headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = &configPtr->headings.elems[headingIndex];
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = &configPtr->items.elems[j];

                OutputWcs(L"\nextern const ");
                switch (itemPtr->type) {
                    case ITEM_INT:
                        OutputWcs(L"long ");
                        break;

                    case ITEM_UINT:
                        OutputWcs(L"unsigned long ");
                        break;

                    case ITEM_FLOAT:
                        OutputWcs(L"double ");
                        break;

                    case ITEM_WSTR:
                        OutputWcs(L"wchar_t ");
                        break;
                }

                OutputWstr(&configPtr->name);
                OutputWcs(L"Default_");
                OutputWstr(&itemPtr->name);

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"[");
                    OutputWstr(&itemPtr->length);
                    OutputWcs(L"]");
                }

                OutputWcs(L";");
            }
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//----------");
        OutputWcs(L"\n// Functions");

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\nvoid ");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Init(");
            OutputWstr(&configPtr->name);
            OutputWcs(L" * configPtr);");

            OutputWcs(L"\n\nbool ");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Load(");
            OutputWstr(&configPtr->name);
            OutputWcs(L" * configPtr, const wchar_t * configWcs, size_t configLength, MkConfGenLoadError ** errors, size_t * errorCount);");
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
        OutputWstr(&inputHead);

        OutputWcs(L"\n");
        OutputWcs(L"\n//-----");
        OutputWcs(L"\n// Keys");

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\nconst size_t _mkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Indices[] = {");

            size_t currentKeyIndex = 0;
            wchar_t tmpBuffer[32];
            for (size_t j = 0; j != configPtr->items.count; j++) {
                swprintf_s(tmpBuffer, 32, L"\n    %zu,", currentKeyIndex);
                OutputWcs(tmpBuffer);

                Item * itemPtr = &configPtr->items.elems[j];
                currentKeyIndex += itemPtr->name.length;
            }
            swprintf_s(tmpBuffer, 32, L"\n    %zu,", currentKeyIndex);
            OutputWcs(tmpBuffer);
            OutputWcs(L"\n};");

            OutputWcs(L"\n\nconst wchar_t _mkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Keys[] =");

            for (size_t j = 0; j != configPtr->items.count; j++) {
                Item * itemPtr = &configPtr->items.elems[j];
                OutputWcs(L"\n    L\"");
                OutputWstr(&itemPtr->name);
                OutputWcs(L"\"");
            }
            OutputWcs(L";");
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//---------------");
        OutputWcs(L"\n// Default Values");

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\n// ");
            OutputWstr(&configPtr->name);

            Heading * headingPtr;
            size_t headingIndex = 0;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = &configPtr->headings.elems[headingIndex];
            } else {
                headingPtr = NULL;
            }

            for (size_t j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n// ");
                    OutputWstr(&headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = &configPtr->headings.elems[headingIndex];
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = &configPtr->items.elems[j];

                OutputWcs(L"\nconst ");
                switch (itemPtr->type) {
                    case ITEM_INT:
                        OutputWcs(L"long ");
                        break;

                    case ITEM_UINT:
                        OutputWcs(L"unsigned long ");
                        break;

                    case ITEM_FLOAT:
                        OutputWcs(L"double ");
                        break;

                    case ITEM_WSTR:
                        OutputWcs(L"wchar_t ");
                        break;
                }

                OutputWstr(&configPtr->name);
                OutputWcs(L"Default_");
                OutputWstr(&itemPtr->name);

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"[");
                    OutputWstr(&itemPtr->length);
                    OutputWcs(L"] = L\"");
                    OutputWstr(&itemPtr->defaultValue);
                    OutputWcs(L"\";");
                } else {
                    OutputWcs(L" = ");
                    OutputWstr(&itemPtr->defaultValue);
                    OutputWcs(L";");
                }
            }
        }

        OutputWcs(L"\n");
        OutputWcs(L"\n//----------");
        OutputWcs(L"\n// Functions");

        for (size_t i = 0; i != configs.count; i++) {
            Config * configPtr = &configs.elems[i];

            OutputWcs(L"\n\nvoid ");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Init(");
            OutputWstr(&configPtr->name);
            OutputWcs(L" * configPtr) {");
            OutputWcs(L"\n    _MKCONFGEN_ASSERT(configPtr);");

            size_t headingIndex = 0;
            Heading * headingPtr;
            if (headingIndex != configPtr->headings.count) {
                headingPtr = &configPtr->headings.elems[headingIndex];
            } else {
                headingPtr = NULL;
            }

            for (size_t j = 0; j != configPtr->items.count; j++) {
                if (headingPtr != NULL && j == headingPtr->index) {
                    OutputWcs(L"\n\n    // ");
                    OutputWstr(&headingPtr->name);

                    if (++headingIndex != configPtr->headings.count) {
                        headingPtr = &configPtr->headings.elems[headingIndex];
                    } else {
                        headingPtr = NULL;
                    }
                }

                Item * itemPtr = &configPtr->items.elems[j];

                if (itemPtr->type == ITEM_WSTR) {
                    OutputWcs(L"\n    wcscpy_s(configPtr->");
                    OutputWstr(&itemPtr->name);
                    OutputWcs(L", ");
                    OutputWstr(&itemPtr->length);
                    OutputWcs(L", ");
                    OutputWstr(&configPtr->name);
                    OutputWcs(L"Default_");
                    OutputWstr(&itemPtr->name);
                    OutputWcs(L");");
                } else {
                    OutputWcs(L"\n    configPtr->");
                    OutputWstr(&itemPtr->name);
                    OutputWcs(L" = ");
                    OutputWstr(&configPtr->name);
                    OutputWcs(L"Default_");
                    OutputWstr(&itemPtr->name);
                    OutputWcs(L";");
                }
            }

            OutputWcs(L"\n}");

            OutputWcs(L"\n\nstatic bool _MkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"ParseValue(");
            OutputWcs(L"\n    void * config,");
            OutputWcs(L"\n    size_t index,");
            OutputWcs(L"\n    wchar_t * rawValue,");
            OutputWcs(L"\n    size_t rawValueLength,");
            OutputWcs(L"\n    bool isStr,");
            OutputWcs(L"\n    MkConfGenLoadErrorType * errorType)");
            OutputWcs(L"\n{");

            OutputWcs(L"\n    ");
            OutputWstr(&configPtr->name);
            OutputWcs(L" * configPtr = (");
            OutputWstr(&configPtr->name);
            OutputWcs(L" *)config;");

            OutputWcs(L"\n    switch (index) {");

            for (size_t j = 0; j != configPtr->items.count; j++) {
                Item * itemPtr = &configPtr->items.elems[j];

                wchar_t tmpBuffer[64];
                swprintf_s(tmpBuffer, 64, L"\n\n        case %zu:", j);
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
                            OutputWstr(&itemPtr->validateCallback);
                            OutputWcs(L"(value)) {");
                            OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_INVALID;");
                            OutputWcs(L"\n                return false;");
                            OutputWcs(L"\n            }");
                        }

                        OutputWcs(L"\n            configPtr->");
                        OutputWstr(&itemPtr->name);
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
                            OutputWstr(&itemPtr->validateCallback);
                            OutputWcs(L"(value)) {");
                            OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_INVALID;");
                            OutputWcs(L"\n                return false;");
                            OutputWcs(L"\n            }");
                        }

                        OutputWcs(L"\n            configPtr->");
                        OutputWstr(&itemPtr->name);
                        OutputWcs(L" = value;");

                        OutputWcs(L"\n            return true;");
                        OutputWcs(L"\n        }");
                        break;
                    }

                    case ITEM_FLOAT:
                    {
                        OutputWcs(L"\n            if (isStr) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n            ");
                        OutputWcs(L"\n            wchar_t * end;");
                        OutputWcs(L"\n            double value = wcstod(rawValue, &end);");
                        OutputWcs(L"\n            if (end != rawValue + rawValueLength) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_TYPE;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");
                        OutputWcs(L"\n            if (value == HUGE_VAL || value == -HUGE_VAL) {");
                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        if (itemPtr->validateCallback.length != 0) {
                            OutputWcs(L"\n            if (!");
                            OutputWstr(&itemPtr->validateCallback);
                            OutputWcs(L"(value)) {");
                            OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_INVALID;");
                            OutputWcs(L"\n                return false;");
                            OutputWcs(L"\n            }");
                        }

                        OutputWcs(L"\n            configPtr->");
                        OutputWstr(&itemPtr->name);
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

                        OutputWcs(L"\n            if (rawValueLength >= ");
                        OutputWstr(&itemPtr->length);
                        OutputWcs(L") { ");

                        OutputWcs(L"\n                *errorType = MKCONFGEN_LOAD_ERROR_VALUE_OVERFLOW;");
                        OutputWcs(L"\n                return false;");
                        OutputWcs(L"\n            }");

                        OutputWcs(L"\n            wcscpy_s(configPtr->");
                        OutputWstr(&itemPtr->name);
                        OutputWcs(L", ");
                        OutputWstr(&itemPtr->length);
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

            OutputWcs(L"\n\nbool ");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Load(");
            OutputWstr(&configPtr->name);
            OutputWcs(L" * configPtr, const wchar_t * configWcs, size_t configLength, MkConfGenLoadError ** errors, size_t * errorCount) {");
            OutputWcs(L"\n    return _MkConfGenLoad(");
            OutputWcs(L"\n        configWcs,");
            OutputWcs(L"\n        configLength,");

            wchar_t tmpBuffer[32];
            swprintf_s(tmpBuffer, 32, L"\n        %zu,", configPtr->items.count);
            OutputWcs(tmpBuffer);

            OutputWcs(L"\n        _mkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Indices,");

            OutputWcs(L"\n        _mkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"Keys,");

            OutputWcs(L"\n        _MkConfGen");
            OutputWstr(&configPtr->name);
            OutputWcs(L"ParseValue,");


            OutputWcs(L"\n        configPtr,");
            OutputWcs(L"\n        errors,");
            OutputWcs(L"\n        errorCount);");

            OutputWcs(L"\n}");
        }

        CloseHandle(file);
    }

    for (size_t i = 0; i != configs.count; i++) {
        Config * configPtr = &configs.elems[i];

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

        size_t headingIndex = 0;
        Heading * headingPtr;
        if (headingIndex != configPtr->headings.count) {
            headingPtr = &configPtr->headings.elems[headingIndex];
            if (headingPtr->index == 0) {
                OutputWcs(L"# ");
                OutputWstr(&headingPtr->name);

                if (++headingIndex != configPtr->headings.count) {
                    headingPtr = &configPtr->headings.elems[headingIndex];
                } else {
                    headingPtr = NULL;
                }
            }
        } else {
            headingPtr = NULL;
        }

        for (size_t j = 0; j != configPtr->items.count; j++) {
            if (headingPtr != NULL && headingPtr->index == j) {
                OutputWcs(L"\n\n# ");
                OutputWstr(&headingPtr->name);

                if (++headingIndex != configPtr->headings.count) {
                    headingPtr = &configPtr->headings.elems[headingIndex];
                } else {
                    headingPtr = NULL;
                }
            }

            Item * itemPtr = &configPtr->items.elems[j];
            OutputWcs(L"\n");
            OutputWstr(&itemPtr->name);
            OutputWcs(L" = ");
            if (itemPtr->type == ITEM_WSTR) {
                OutputWcs(L"\"");
            }
            OutputWstr(&itemPtr->defaultValue);
            if (itemPtr->type == ITEM_WSTR) {
                OutputWcs(L"\"");
            }
        }

        CloseHandle(file);
    }

    return 0;
}