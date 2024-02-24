# Purpose

This is a metaprogram that generates configuration file handling code for C++ programs.

# Important Notes

This program is currently Windows-only since it uses Win32 functions for reading and writing files. Porting shouldn't be that hard once necessary.

The input file is expected to use UTF-8 encoding without BOM. Both LF and CR+LF line ending styles are supported for input, output uses CR+LF since we are Windows-exclusive.

Because this is just a side-project for use in my other projects, it's somewhat rough because I just wanted it to work and didn't put much effort into nice error messages or code organization. Maybe later...

I haven't added support for floating point items yet.

# How to use

1. Add the header/implementation pair from the `Deploy` folder into your project.
2. Create the `.cpp` file containing the definitions (see next chapter).
3. You can compile the definition file in debug mode (meaning `NDEBUG` is not defined) to check for syntax errors and duplicates.
4. Now run `MkConfGen.exe <PATH_TO_DEFINITION_FILE>` to generate a header/implementation pair. Example config files will also be created. The program returns these codes:
   - 0 - OK
   - 1 - input file could not be opened/read
   - 2 - not enough memory
   - 3 - syntax error
   - 4 - output files could not be written
6. The generated code files contain:
   - structs for the actual config values
   - `Init` functions that initialize a config struct with default values
   - `Load` functions to read values from a config file
7. To use the `Load` function, you have to provide a callback function that reads the next character from a given stream.

# Definition File

*There should be an example in the `Testdata` folder.*

The first line must be the `#include` directive of the `MkConfGen.h` file from the `Deploy` folder.

The actual config definitions are placed between the `MKCONFGEN_FILE_BEGIN` and `MKCONFGEN_FILE_END` statements. Everything before that will be copied into the target `.cpp` file, everything after will just be ignored.

A definition file can contain multiple config definitions. Each config definition will have its own struct and config file afterwards. A config definition is introduced with `MKCONFGEN_DEF_BEGIN(<ConfigName>)` and ends with `MKCONFGEN_DEF_END`.

The following config item types can be defined:

- `MKCONFGEN_ITEM_INT(<itemName>, <defaultValue>)` - long integer
- `MKCONFGEN_ITEM_UINT(<itemName>, <defaultValue>)` - unsigned long integer
- `MKCONFGEN_ITEM_WSTR(<itemName>, <size>, <defaultValue>)`
  - wide string containing `<size> - 1` characters (NULL-terminator matters here)
  - `<defaultValue>` must be a wide string literal (which means `L"text"`)
  - the only supported escape code is `\"`

You can also add callbacks to validation functions with the statement `MKCONFGEN_VALIDATE(<itemName>, <CallbackName>)` for already defined items. These functions take a value of matching type and return a `bool` to signal whether the given value was valid or not.

Furthermore, you can introduce headings anywhere in the list of items using `MKCONFGEN_HEADING(<Text>)`. These do nothing in terms of logic but add comments to code and config files for better readability.

# Config File

*There should be an example in the `Testdata` folder.*

A config file contains one item per non-empty line. The pattern is simply `name = value`, with string values enclosed in double quotes (like `"test"`).

Comments can start anywhere on a line with the `#` character.

# Other

*Gloria in excelsis Deo*
