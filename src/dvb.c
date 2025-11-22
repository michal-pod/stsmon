/*
 * This file is part of stsmon - a simple DVB transport stream monitor
 * Copyright (C) 2025 Michał Podsiadlik <michal@nglab.net>
 * 
 * stsmon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * stsmon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with stsmon. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#ifdef WIN32
#include <windows.h>
#endif
#ifndef WIN32
#include <iconv.h>
#endif
#include <bitstream/dvb/si/strings.h>
#include <ctype.h>
#include <string.h>
#include "output.h"


#ifdef WIN32
WCHAR* iso9737_to_wide(const uint8_t* str, size_t length){
    // Simple ISO6937 to UCS-2BE conversion for Windows
    // Note: This is simplified conversion just to make ISO6937 strings readable on console.
    // It does not handle all diacritics and special characters.
    size_t wideLength = length; // Worst case: 1 byte -> 1 wide char
    WCHAR* wideStr = (WCHAR*)malloc((wideLength + 1) * sizeof(WCHAR));
    size_t j = 0;
    for(size_t i = 0; i < length; i++){
        uint8_t byte = str[i];
        if(byte < 0x80){
            // ASCII range
            wideStr[j++] = (WCHAR)byte;
        }
        else if(byte >= 0xC0){
            // Skip diacritics for simplicity            
        }
        else{
            // Unsupported character, replace with space
            wideStr[j++] = L'?';
        }
    }
    wideStr[j] = 0;
    return wideStr;
}
#endif

/*
 * Decode DVB-encoded strings (which may start with a language/charset
 * identifier). Returns a newly-allocated UTF-8 string which the caller
 * must free. On failure the original bytes are copied into a NUL-terminated
 * buffer as a fallback.
 */
char* dvb_string_decode(const uint8_t* str, size_t length){
    const char* encoding = dvb_string_get_encoding(&str, &length, "ISO6937");
    if(encoding == NULL || !length){
        /* No encoding info or empty string: copy raw bytes as-is */
        char* result = (char*)malloc(length + 1);
        memcpy(result, str, length);
        result[length] = '\0';
        return result;
    }
    #ifdef WIN32
    /*
     * This is very lightweight utility, libiconv will be several times bigger
     * than the whole stsmon executable. So we implement minimal encoding
     * conversion using Windows API here.
     */
    enum {
        ENCODING_UTF8 = 65001,
        ENCODING_UCS2BE = 1201,
        ENCODING_GB2312 = 936,
        ENCODING_EUCKR = 51949,
        ENCODING_ISO8859_1 = 28591,
        ENCODING_ISO8859_2 = 28592,
        ENCODING_ISO8859_5 = 28595,
        ENCODING_ISO8859_6 = 28596,
        ENCODING_ISO8859_7 = 28597,
        ENCODING_ISO8859_8 = 28598,
        ENCODING_ISO8859_9 = 28599,
        ENCODING_ISO8859_13 = 28603,
        ENCODING_ISO8859_15 = 28605,
        ENCODING_ISO6937 = 0xffff,
        ENCODING_UNKNOWN = 0
    };
    UINT codepage = ENCODING_UNKNOWN;
    BOOL isWide = FALSE;

    /* First step, convert encoding to windows codepage */
    if (!strcasecmp(encoding, "UTF-8"))
        codepage = ENCODING_UTF8;
    else if (!strcasecmp(encoding, "UCS-2BE")) {
        codepage = ENCODING_UCS2BE;
        isWide = TRUE;
    }
    else if (!strcasecmp(encoding, "GB2312") || !strcasecmp(encoding, "GBK"))
        codepage = ENCODING_GB2312;
    else if (!strcasecmp(encoding, "EUC-KR"))
        codepage = ENCODING_EUCKR;
    else if (!strcasecmp(encoding, "ISO-8859-1"))
        codepage = ENCODING_ISO8859_1;
    else if (!strcasecmp(encoding, "ISO-8859-2"))
        codepage = ENCODING_ISO8859_2;
    else if (!strcasecmp(encoding, "ISO-8859-5"))
        codepage = ENCODING_ISO8859_5   ;
    else if (!strcasecmp(encoding, "ISO-8859-6"))
        codepage = ENCODING_ISO8859_6;
    else if (!strcasecmp(encoding, "ISO-8859-7"))
        codepage = ENCODING_ISO8859_7;
    else if (!strcasecmp(encoding, "ISO-8859-8"))
        codepage = ENCODING_ISO8859_8;
    else if (!strcasecmp(encoding, "ISO-8859-9"))
        codepage = ENCODING_ISO8859_9;
    else if (!strcasecmp(encoding, "ISO-8859-13"))
        codepage = ENCODING_ISO8859_13;
    else if (!strcasecmp(encoding, "ISO-8859-15"))
        codepage = ENCODING_ISO8859_15;
    else if (!strcasecmp(encoding, "ISO6937"))
        codepage = ENCODING_ISO6937;

    /* We are printing to console, so we need to convert to console codepage not CP_ACP */
    DWORD consoleCP = GetConsoleCP();

    /* Now we need to get windows compactible wide char representation of string */
    WCHAR* wideStr = NULL;
    if(isWide){
        // Convert from UCS-2BE to wide char
        int wideLength = (int)(length / 2);
        wideStr = (WCHAR*)malloc((wideLength + 1) * sizeof(WCHAR));
        for(int i = 0; i < wideLength; i++){
            wideStr[i] = (WCHAR)((str[2*i] << 8) | str[2*i + 1]);
        }
        wideStr[wideLength] = 0;
    }
    else if(codepage == ENCODING_ISO6937){
        // Special handling for ISO6937
        wideStr = iso9737_to_wide(str, length);
    }
    else if(codepage != ENCODING_UNKNOWN){
        // Convert from specified codepage to wide char
        int wideLength = MultiByteToWideChar(codepage, 0, (LPCSTR)str, (int)length, NULL, 0);
        wideStr = (WCHAR*)malloc((wideLength + 1) * sizeof(WCHAR));
        MultiByteToWideChar(codepage, 0, (LPCSTR)str, (int)length, wideStr, wideLength);
        wideStr[wideLength] = 0;
    }
    else
    {
        // Unknown encoding, return raw bytes as fallback
        char* result = (char*)malloc(length + 1);
        memcpy(result, str, length);
        result[length] = '\0';
        return result;
    }

    if(!wideStr){
        // Conversion failed, return raw bytes as fallback
        char* result = (char*)malloc(length + 1);
        memcpy(result, str, length);
        result[length] = '\0';
        return result;
    }

    /* Finally, convert from wide char to console code page */
    int ccpLength = WideCharToMultiByte(consoleCP, 0, wideStr, -1, NULL, 0, NULL, NULL);
    char* ccpStr = (char*)malloc(ccpLength);
    WideCharToMultiByte(consoleCP, 0, wideStr, -1, ccpStr, ccpLength, NULL, NULL);
    free(wideStr);
    return ccpStr;
    
    #else
    iconv_t cd = iconv_open("UTF-8", encoding);
    if(cd == (iconv_t)-1){
        /* failed to open converter, return original string */
        char* result = (char*)malloc(length + 1);
        memcpy(result, str, length);
        result[length] = '\0';
        return result;
    }
    size_t in_bytes_left = length;
    size_t out_bytes_left = length * 6; // allocate enough space
    char* outbuf = (char*)malloc(out_bytes_left + 1);
    char* outptr = outbuf;
    char* inbuf = (char*)str;
    if(iconv(cd, &inbuf, &in_bytes_left, &outptr, &out_bytes_left) == (size_t)-1){
        /* conversion failed, return original string (fallback) */
        free(outbuf);
        iconv_close(cd);
        char* result = (char*)malloc(length + 1);
        memcpy(result, str, length);
        result[length] = '\0';
        return result;
    }
    *outptr = '\0';
    iconv_close(cd);
    return outbuf;
    #endif
}
