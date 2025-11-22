// ðŸ˜
#pragma code_page(65001)
#include <windows.h>
1 VERSIONINFO
FILEVERSION     @VERSION_Y@, @VERSION_M@, @VERSION_D@, @VERSION_B@
PRODUCTVERSION  @VERSION_Y@, @VERSION_M@, @VERSION_D@, @VERSION_B@
FILEOS          0x40004
FILETYPE        0x1
FILESUBTYPE     0x0
{
    BLOCK "StringFileInfo"
    {
        BLOCK "040904E4"
        {
            VALUE "CompanyName", "nglab.net\0"
            VALUE "FileDescription", "stsmon - simple transport stream monitor\0"
            VALUE "FileVersion", "@VERSION@\0"
            VALUE "LegalCopyright", "Copyright (C) 20@VERSION_Y@ Michał‚ Podsiadlik. License GPLv3+\0"
            VALUE "ProductName", "stsmon\0"
            VALUE "ProductVersion", "@VERSION@\0"
            VALUE "Comments", "This is free software licensed under GPLv3+. There is NO WARRANTY. See https://github.com/michal-pod/stsmon for more information.\0"
        }
    }
    BLOCK "VarFileInfo"
    {
        VALUE "Translation", 0x0409, 0x04E4
    }
}
