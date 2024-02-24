#include "Import/MkConfGen.h"

static bool ValidateFontSize(int value) {
    return value > 0;
}

static bool ValidateColor(unsigned long value) {
    return value <= 0xffffff;
}

MKCONFGEN_FILE_BEGIN

MKCONFGEN_DEF_BEGIN(Config)

MKCONFGEN_HEADING(General)
MKCONFGEN_ITEM_WSTR(fontName, 32, L"Consolas")
MKCONFGEN_ITEM_INT(fontSize, 10)
MKCONFGEN_ITEM_UINT(tabWidth, 4)
MKCONFGEN_ITEM_INT(expandTabs, 0)
MKCONFGEN_ITEM_INT(useVimMode, 0)

MKCONFGEN_VALIDATE(fontSize, ValidateFontSize)

MKCONFGEN_HEADING(Colors)
MKCONFGEN_ITEM_UINT(textColor, 0xdcdcdc)
MKCONFGEN_ITEM_UINT(backgroundColor, 0x1e1e1e)
MKCONFGEN_ITEM_UINT(cursorColor, 0x7d7d7d)
MKCONFGEN_ITEM_UINT(statusBackgroundColor, 0x2e2e2e)
MKCONFGEN_ITEM_UINT(docTitleBackgroundColor, 0x3d3d3d)
MKCONFGEN_ITEM_UINT(promptTextColor, 0xffffff)
MKCONFGEN_ITEM_UINT(promptBackgroundColor, 0x861b2d)

MKCONFGEN_VALIDATE(textColor, ValidateColor)
MKCONFGEN_VALIDATE(backgroundColor, ValidateColor)
MKCONFGEN_VALIDATE(cursorColor, ValidateColor)
MKCONFGEN_VALIDATE(statusBackgroundColor, ValidateColor)
MKCONFGEN_VALIDATE(docTitleBackgroundColor, ValidateColor)
MKCONFGEN_VALIDATE(promptTextColor, ValidateColor)
MKCONFGEN_VALIDATE(promptBackgroundColor, ValidateColor)

MKCONFGEN_DEF_END

MKCONFGEN_FILE_END