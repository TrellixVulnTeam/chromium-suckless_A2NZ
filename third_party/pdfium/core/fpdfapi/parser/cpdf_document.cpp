// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/parser/cpdf_document.h"

#include <memory>
#include <set>
#include <vector>

#include "core/fpdfapi/cpdf_modulemgr.h"
#include "core/fpdfapi/font/cpdf_fontencoding.h"
#include "core/fpdfapi/page/cpdf_pagemodule.h"
#include "core/fpdfapi/page/pageint.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/render/render_int.h"
#include "core/fxcodec/JBig2_DocumentContext.h"
#include "core/fxge/cfx_unicodeencoding.h"
#include "core/fxge/fx_font.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"

namespace {

const int FX_MAX_PAGE_LEVEL = 1024;

const uint16_t g_FX_CP874Unicodes[128] = {
    0x20AC, 0x0000, 0x0000, 0x0000, 0x0000, 0x2026, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00A0, 0x0E01, 0x0E02, 0x0E03,
    0x0E04, 0x0E05, 0x0E06, 0x0E07, 0x0E08, 0x0E09, 0x0E0A, 0x0E0B, 0x0E0C,
    0x0E0D, 0x0E0E, 0x0E0F, 0x0E10, 0x0E11, 0x0E12, 0x0E13, 0x0E14, 0x0E15,
    0x0E16, 0x0E17, 0x0E18, 0x0E19, 0x0E1A, 0x0E1B, 0x0E1C, 0x0E1D, 0x0E1E,
    0x0E1F, 0x0E20, 0x0E21, 0x0E22, 0x0E23, 0x0E24, 0x0E25, 0x0E26, 0x0E27,
    0x0E28, 0x0E29, 0x0E2A, 0x0E2B, 0x0E2C, 0x0E2D, 0x0E2E, 0x0E2F, 0x0E30,
    0x0E31, 0x0E32, 0x0E33, 0x0E34, 0x0E35, 0x0E36, 0x0E37, 0x0E38, 0x0E39,
    0x0E3A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0E3F, 0x0E40, 0x0E41, 0x0E42,
    0x0E43, 0x0E44, 0x0E45, 0x0E46, 0x0E47, 0x0E48, 0x0E49, 0x0E4A, 0x0E4B,
    0x0E4C, 0x0E4D, 0x0E4E, 0x0E4F, 0x0E50, 0x0E51, 0x0E52, 0x0E53, 0x0E54,
    0x0E55, 0x0E56, 0x0E57, 0x0E58, 0x0E59, 0x0E5A, 0x0E5B, 0x0000, 0x0000,
    0x0000, 0x0000,
};
const uint16_t g_FX_CP1250Unicodes[128] = {
    0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021, 0x0000,
    0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0161,
    0x203A, 0x015B, 0x0165, 0x017E, 0x017A, 0x00A0, 0x02C7, 0x02D8, 0x0141,
    0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x017B, 0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5,
    0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E,
    0x017C, 0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, 0x0110,
    0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E,
    0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, 0x0155, 0x00E1, 0x00E2,
    0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB,
    0x011B, 0x00ED, 0x00EE, 0x010F, 0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4,
    0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD,
    0x0163, 0x02D9,
};
const uint16_t g_FX_CP1251Unicodes[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC,
    0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0459,
    0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408,
    0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5,
    0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455,
    0x0457, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x0420,
    0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429,
    0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432,
    0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B,
    0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
    0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D,
    0x044E, 0x044F,
};
const uint16_t g_FX_CP1253Unicodes[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x0000,
    0x2030, 0x0000, 0x2039, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0000,
    0x203A, 0x0000, 0x0000, 0x0000, 0x0000, 0x00A0, 0x0385, 0x0386, 0x00A3,
    0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x0000, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x2015, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x0384, 0x00B5,
    0x00B6, 0x00B7, 0x0388, 0x0389, 0x038A, 0x00BB, 0x038C, 0x00BD, 0x038E,
    0x038F, 0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
    0x0398, 0x0399, 0x039A, 0x039B, 0x039C, 0x039D, 0x039E, 0x039F, 0x03A0,
    0x03A1, 0x0000, 0x03A3, 0x03A4, 0x03A5, 0x03A6, 0x03A7, 0x03A8, 0x03A9,
    0x03AA, 0x03AB, 0x03AC, 0x03AD, 0x03AE, 0x03AF, 0x03B0, 0x03B1, 0x03B2,
    0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7, 0x03B8, 0x03B9, 0x03BA, 0x03BB,
    0x03BC, 0x03BD, 0x03BE, 0x03BF, 0x03C0, 0x03C1, 0x03C2, 0x03C3, 0x03C4,
    0x03C5, 0x03C6, 0x03C7, 0x03C8, 0x03C9, 0x03CA, 0x03CB, 0x03CC, 0x03CD,
    0x03CE, 0x0000,
};
const uint16_t g_FX_CP1254Unicodes[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6,
    0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x0000, 0x0000, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161,
    0x203A, 0x0153, 0x0000, 0x0000, 0x0178, 0x00A0, 0x00A1, 0x00A2, 0x00A3,
    0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5,
    0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE,
    0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, 0x011E,
    0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9,
    0x00DA, 0x00DB, 0x00DC, 0x0130, 0x015E, 0x00DF, 0x00E0, 0x00E1, 0x00E2,
    0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB,
    0x00EC, 0x00ED, 0x00EE, 0x00EF, 0x011F, 0x00F1, 0x00F2, 0x00F3, 0x00F4,
    0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x0131,
    0x015F, 0x00FF,
};
const uint16_t g_FX_CP1255Unicodes[128] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6,
    0x2030, 0x0000, 0x2039, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0000,
    0x203A, 0x0000, 0x0000, 0x0000, 0x0000, 0x00A0, 0x00A1, 0x00A2, 0x00A3,
    0x20AA, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00D7, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5,
    0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00F7, 0x00BB, 0x00BC, 0x00BD, 0x00BE,
    0x00BF, 0x05B0, 0x05B1, 0x05B2, 0x05B3, 0x05B4, 0x05B5, 0x05B6, 0x05B7,
    0x05B8, 0x05B9, 0x0000, 0x05BB, 0x05BC, 0x05BD, 0x05BE, 0x05BF, 0x05C0,
    0x05C1, 0x05C2, 0x05C3, 0x05F0, 0x05F1, 0x05F2, 0x05F3, 0x05F4, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x05D0, 0x05D1, 0x05D2,
    0x05D3, 0x05D4, 0x05D5, 0x05D6, 0x05D7, 0x05D8, 0x05D9, 0x05DA, 0x05DB,
    0x05DC, 0x05DD, 0x05DE, 0x05DF, 0x05E0, 0x05E1, 0x05E2, 0x05E3, 0x05E4,
    0x05E5, 0x05E6, 0x05E7, 0x05E8, 0x05E9, 0x05EA, 0x0000, 0x0000, 0x200E,
    0x200F, 0x0000,
};
const uint16_t g_FX_CP1256Unicodes[128] = {
    0x20AC, 0x067E, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6,
    0x2030, 0x0679, 0x2039, 0x0152, 0x0686, 0x0698, 0x0688, 0x06AF, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x06A9, 0x2122, 0x0691,
    0x203A, 0x0153, 0x200C, 0x200D, 0x06BA, 0x00A0, 0x060C, 0x00A2, 0x00A3,
    0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x06BE, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5,
    0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x061B, 0x00BB, 0x00BC, 0x00BD, 0x00BE,
    0x061F, 0x06C1, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627,
    0x0628, 0x0629, 0x062A, 0x062B, 0x062C, 0x062D, 0x062E, 0x062F, 0x0630,
    0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x0636, 0x00D7, 0x0637, 0x0638,
    0x0639, 0x063A, 0x0640, 0x0641, 0x0642, 0x0643, 0x00E0, 0x0644, 0x00E2,
    0x0645, 0x0646, 0x0647, 0x0648, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB,
    0x0649, 0x064A, 0x00EE, 0x00EF, 0x064B, 0x064C, 0x064D, 0x064E, 0x00F4,
    0x064F, 0x0650, 0x00F7, 0x0651, 0x00F9, 0x0652, 0x00FB, 0x00FC, 0x200E,
    0x200F, 0x06D2,
};
const uint16_t g_FX_CP1257Unicodes[128] = {
    0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021, 0x0000,
    0x2030, 0x0000, 0x2039, 0x0000, 0x00A8, 0x02C7, 0x00B8, 0x0000, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0000,
    0x203A, 0x0000, 0x00AF, 0x02DB, 0x0000, 0x00A0, 0x0000, 0x00A2, 0x00A3,
    0x00A4, 0x0000, 0x00A6, 0x00A7, 0x00D8, 0x00A9, 0x0156, 0x00AB, 0x00AC,
    0x00AD, 0x00AE, 0x00C6, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5,
    0x00B6, 0x00B7, 0x00F8, 0x00B9, 0x0157, 0x00BB, 0x00BC, 0x00BD, 0x00BE,
    0x00E6, 0x0104, 0x012E, 0x0100, 0x0106, 0x00C4, 0x00C5, 0x0118, 0x0112,
    0x010C, 0x00C9, 0x0179, 0x0116, 0x0122, 0x0136, 0x012A, 0x013B, 0x0160,
    0x0143, 0x0145, 0x00D3, 0x014C, 0x00D5, 0x00D6, 0x00D7, 0x0172, 0x0141,
    0x015A, 0x016A, 0x00DC, 0x017B, 0x017D, 0x00DF, 0x0105, 0x012F, 0x0101,
    0x0107, 0x00E4, 0x00E5, 0x0119, 0x0113, 0x010D, 0x00E9, 0x017A, 0x0117,
    0x0123, 0x0137, 0x012B, 0x013C, 0x0161, 0x0144, 0x0146, 0x00F3, 0x014D,
    0x00F5, 0x00F6, 0x00F7, 0x0173, 0x0142, 0x015B, 0x016B, 0x00FC, 0x017C,
    0x017E, 0x02D9,
};

struct FX_CharsetUnicodes {
  uint8_t m_Charset;
  const uint16_t* m_pUnicodes;
};

const FX_CharsetUnicodes g_FX_CharsetUnicodes[] = {
    {FXFONT_THAI_CHARSET, g_FX_CP874Unicodes},
    {FXFONT_EASTEUROPE_CHARSET, g_FX_CP1250Unicodes},
    {FXFONT_RUSSIAN_CHARSET, g_FX_CP1251Unicodes},
    {FXFONT_GREEK_CHARSET, g_FX_CP1253Unicodes},
    {FXFONT_TURKISH_CHARSET, g_FX_CP1254Unicodes},
    {FXFONT_HEBREW_CHARSET, g_FX_CP1255Unicodes},
    {FXFONT_ARABIC_CHARSET, g_FX_CP1256Unicodes},
    {FXFONT_BALTIC_CHARSET, g_FX_CP1257Unicodes},
};

void InsertWidthArrayImpl(int* widths, int size, CPDF_Array* pWidthArray) {
  int i;
  for (i = 1; i < size; i++) {
    if (widths[i] != *widths)
      break;
  }
  if (i == size) {
    int first = pWidthArray->GetIntegerAt(pWidthArray->GetCount() - 1);
    pWidthArray->AddInteger(first + size - 1);
    pWidthArray->AddInteger(*widths);
  } else {
    CPDF_Array* pWidthArray1 = new CPDF_Array;
    pWidthArray->Add(pWidthArray1);
    for (i = 0; i < size; i++)
      pWidthArray1->AddInteger(widths[i]);
  }
  FX_Free(widths);
}

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
void InsertWidthArray(HDC hDC, int start, int end, CPDF_Array* pWidthArray) {
  int size = end - start + 1;
  int* widths = FX_Alloc(int, size);
  GetCharWidth(hDC, start, end, widths);
  InsertWidthArrayImpl(widths, size, pWidthArray);
}

CFX_ByteString FPDF_GetPSNameFromTT(HDC hDC) {
  CFX_ByteString result;
  DWORD size = ::GetFontData(hDC, 'eman', 0, nullptr, 0);
  if (size != GDI_ERROR) {
    LPBYTE buffer = FX_Alloc(BYTE, size);
    ::GetFontData(hDC, 'eman', 0, buffer, size);
    result = GetNameFromTT(buffer, size, 6);
    FX_Free(buffer);
  }
  return result;
}
#endif  // _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_

void InsertWidthArray1(CFX_Font* pFont,
                       CFX_UnicodeEncoding* pEncoding,
                       FX_WCHAR start,
                       FX_WCHAR end,
                       CPDF_Array* pWidthArray) {
  int size = end - start + 1;
  int* widths = FX_Alloc(int, size);
  int i;
  for (i = 0; i < size; i++) {
    int glyph_index = pEncoding->GlyphFromCharCode(start + i);
    widths[i] = pFont->GetGlyphWidth(glyph_index);
  }
  InsertWidthArrayImpl(widths, size, pWidthArray);
}

int InsertDeletePDFPage(CPDF_Document* pDoc,
                        CPDF_Dictionary* pPages,
                        int nPagesToGo,
                        CPDF_Dictionary* pPage,
                        FX_BOOL bInsert,
                        std::set<CPDF_Dictionary*>* pVisited) {
  CPDF_Array* pKidList = pPages->GetArrayFor("Kids");
  if (!pKidList)
    return -1;

  for (size_t i = 0; i < pKidList->GetCount(); i++) {
    CPDF_Dictionary* pKid = pKidList->GetDictAt(i);
    if (pKid->GetStringFor("Type") == "Page") {
      if (nPagesToGo == 0) {
        if (bInsert) {
          pKidList->InsertAt(i, new CPDF_Reference(pDoc, pPage->GetObjNum()));
          pPage->SetReferenceFor("Parent", pDoc, pPages->GetObjNum());
        } else {
          pKidList->RemoveAt(i);
        }
        pPages->SetIntegerFor(
            "Count", pPages->GetIntegerFor("Count") + (bInsert ? 1 : -1));
        return 1;
      }
      nPagesToGo--;
    } else {
      int nPages = pKid->GetIntegerFor("Count");
      if (nPagesToGo < nPages) {
        if (pdfium::ContainsKey(*pVisited, pKid))
          return -1;

        pdfium::ScopedSetInsertion<CPDF_Dictionary*> insertion(pVisited, pKid);
        if (InsertDeletePDFPage(pDoc, pKid, nPagesToGo, pPage, bInsert,
                                pVisited) < 0) {
          return -1;
        }
        pPages->SetIntegerFor(
            "Count", pPages->GetIntegerFor("Count") + (bInsert ? 1 : -1));
        return 1;
      }
      nPagesToGo -= nPages;
    }
  }
  return 0;
}

int InsertNewPage(CPDF_Document* pDoc,
                  int iPage,
                  CPDF_Dictionary* pPageDict,
                  CFX_ArrayTemplate<uint32_t>& pageList) {
  CPDF_Dictionary* pRoot = pDoc->GetRoot();
  CPDF_Dictionary* pPages = pRoot ? pRoot->GetDictFor("Pages") : nullptr;
  if (!pPages)
    return -1;

  int nPages = pDoc->GetPageCount();
  if (iPage < 0 || iPage > nPages)
    return -1;

  if (iPage == nPages) {
    CPDF_Array* pPagesList = pPages->GetArrayFor("Kids");
    if (!pPagesList) {
      pPagesList = new CPDF_Array;
      pPages->SetFor("Kids", pPagesList);
    }
    pPagesList->Add(new CPDF_Reference(pDoc, pPageDict->GetObjNum()));
    pPages->SetIntegerFor("Count", nPages + 1);
    pPageDict->SetReferenceFor("Parent", pDoc, pPages->GetObjNum());
  } else {
    std::set<CPDF_Dictionary*> stack = {pPages};
    if (InsertDeletePDFPage(pDoc, pPages, iPage, pPageDict, TRUE, &stack) < 0)
      return -1;
  }
  pageList.InsertAt(iPage, pPageDict->GetObjNum());
  return iPage;
}

int CountPages(CPDF_Dictionary* pPages,
               std::set<CPDF_Dictionary*>* visited_pages) {
  int count = pPages->GetIntegerFor("Count");
  if (count > 0 && count < FPDF_PAGE_MAX_NUM)
    return count;
  CPDF_Array* pKidList = pPages->GetArrayFor("Kids");
  if (!pKidList)
    return 0;
  count = 0;
  for (size_t i = 0; i < pKidList->GetCount(); i++) {
    CPDF_Dictionary* pKid = pKidList->GetDictAt(i);
    if (!pKid || pdfium::ContainsKey(*visited_pages, pKid))
      continue;
    if (pKid->KeyExist("Kids")) {
      // Use |visited_pages| to help detect circular references of pages.
      pdfium::ScopedSetInsertion<CPDF_Dictionary*> local_add(visited_pages,
                                                             pKid);
      count += CountPages(pKid, visited_pages);
    } else {
      // This page is a leaf node.
      count++;
    }
  }
  pPages->SetIntegerFor("Count", count);
  return count;
}

int CalculateFlags(bool bold,
                   bool italic,
                   bool fixedPitch,
                   bool serif,
                   bool script,
                   bool symbolic) {
  int flags = 0;
  if (bold)
    flags |= PDFFONT_FORCEBOLD;
  if (italic)
    flags |= PDFFONT_ITALIC;
  if (fixedPitch)
    flags |= PDFFONT_FIXEDPITCH;
  if (serif)
    flags |= PDFFONT_SERIF;
  if (script)
    flags |= PDFFONT_SCRIPT;
  if (symbolic)
    flags |= PDFFONT_SYMBOLIC;
  else
    flags |= PDFFONT_NONSYMBOLIC;
  return flags;
}

void ProcessNonbCJK(CPDF_Dictionary* pBaseDict,
                    bool bold,
                    bool italic,
                    CFX_ByteString basefont,
                    CPDF_Array* pWidths) {
  if (bold && italic)
    basefont += ",BoldItalic";
  else if (bold)
    basefont += ",Bold";
  else if (italic)
    basefont += ",Italic";
  pBaseDict->SetNameFor("Subtype", "TrueType");
  pBaseDict->SetNameFor("BaseFont", basefont);
  pBaseDict->SetNumberFor("FirstChar", 32);
  pBaseDict->SetNumberFor("LastChar", 255);
  pBaseDict->SetFor("Widths", pWidths);
}

CPDF_Dictionary* CalculateFontDesc(CPDF_Document* pDoc,
                                   CFX_ByteString basefont,
                                   int flags,
                                   int italicangle,
                                   int ascend,
                                   int descend,
                                   CPDF_Array* bbox,
                                   int32_t stemV) {
  CPDF_Dictionary* pFontDesc = new CPDF_Dictionary(pDoc->GetByteStringPool());
  pFontDesc->SetNameFor("Type", "FontDescriptor");
  pFontDesc->SetNameFor("FontName", basefont);
  pFontDesc->SetIntegerFor("Flags", flags);
  pFontDesc->SetFor("FontBBox", bbox);
  pFontDesc->SetIntegerFor("ItalicAngle", italicangle);
  pFontDesc->SetIntegerFor("Ascent", ascend);
  pFontDesc->SetIntegerFor("Descent", descend);
  pFontDesc->SetIntegerFor("StemV", stemV);
  return pFontDesc;
}

}  // namespace

CPDF_Document::CPDF_Document(std::unique_ptr<CPDF_Parser> pParser)
    : CPDF_IndirectObjectHolder(),
      m_pParser(std::move(pParser)),
      m_pRootDict(nullptr),
      m_pInfoDict(nullptr),
      m_bLinearized(false),
      m_iFirstPageNo(0),
      m_dwFirstPageObjNum(0),
      m_pDocPage(new CPDF_DocPageData(this)),
      m_pDocRender(new CPDF_DocRenderData(this)),
      m_pByteStringPool(pdfium::MakeUnique<CFX_ByteStringPool>()) {
  if (pParser)
    SetLastObjNum(m_pParser->GetLastObjNum());
}

CPDF_Document::~CPDF_Document() {
  delete m_pDocPage;
  CPDF_ModuleMgr::Get()->GetPageModule()->ClearStockFont(this);
  m_pByteStringPool.DeleteObject();  // Make weak.
}

CPDF_Object* CPDF_Document::ParseIndirectObject(uint32_t objnum) {
  return m_pParser ? m_pParser->ParseIndirectObject(this, objnum) : nullptr;
}

void CPDF_Document::LoadDocInternal() {
  SetLastObjNum(m_pParser->GetLastObjNum());

  CPDF_Object* pRootObj = GetOrParseIndirectObject(m_pParser->GetRootObjNum());
  if (!pRootObj)
    return;

  m_pRootDict = pRootObj->GetDict();
  if (!m_pRootDict)
    return;

  CPDF_Object* pInfoObj = GetOrParseIndirectObject(m_pParser->GetInfoObjNum());
  if (pInfoObj)
    m_pInfoDict = pInfoObj->GetDict();
}

void CPDF_Document::LoadDoc() {
  LoadDocInternal();
  m_PageList.SetSize(RetrievePageCount());
}

void CPDF_Document::LoadLinearizedDoc(CPDF_Dictionary* pLinearizationParams) {
  m_bLinearized = true;
  LoadDocInternal();

  uint32_t dwPageCount = 0;
  CPDF_Object* pCount = pLinearizationParams->GetObjectFor("N");
  if (ToNumber(pCount))
    dwPageCount = pCount->GetInteger();
  m_PageList.SetSize(dwPageCount);

  CPDF_Object* pNo = pLinearizationParams->GetObjectFor("P");
  if (ToNumber(pNo))
    m_iFirstPageNo = pNo->GetInteger();

  CPDF_Object* pObjNum = pLinearizationParams->GetObjectFor("O");
  if (ToNumber(pObjNum))
    m_dwFirstPageObjNum = pObjNum->GetInteger();
}

void CPDF_Document::LoadPages() {
  m_PageList.SetSize(RetrievePageCount());
}

CPDF_Dictionary* CPDF_Document::FindPDFPage(CPDF_Dictionary* pPages,
                                            int iPage,
                                            int nPagesToGo,
                                            int level) {
  CPDF_Array* pKidList = pPages->GetArrayFor("Kids");
  if (!pKidList)
    return nPagesToGo == 0 ? pPages : nullptr;

  if (level >= FX_MAX_PAGE_LEVEL)
    return nullptr;

  for (size_t i = 0; i < pKidList->GetCount(); i++) {
    CPDF_Dictionary* pKid = pKidList->GetDictAt(i);
    if (!pKid) {
      nPagesToGo--;
      continue;
    }
    if (pKid == pPages)
      continue;
    if (!pKid->KeyExist("Kids")) {
      if (nPagesToGo == 0)
        return pKid;

      m_PageList.SetAt(iPage - nPagesToGo, pKid->GetObjNum());
      nPagesToGo--;
    } else {
      int nPages = pKid->GetIntegerFor("Count");
      if (nPagesToGo < nPages)
        return FindPDFPage(pKid, iPage, nPagesToGo, level + 1);

      nPagesToGo -= nPages;
    }
  }
  return nullptr;
}

CPDF_Dictionary* CPDF_Document::GetPagesDict() const {
  CPDF_Dictionary* pRoot = GetRoot();
  return pRoot ? pRoot->GetDictFor("Pages") : nullptr;
}

bool CPDF_Document::IsPageLoaded(int iPage) const {
  return !!m_PageList.GetAt(iPage);
}

CPDF_Dictionary* CPDF_Document::GetPage(int iPage) {
  if (iPage < 0 || iPage >= m_PageList.GetSize())
    return nullptr;

  if (m_bLinearized && (iPage == m_iFirstPageNo)) {
    if (CPDF_Dictionary* pDict =
            ToDictionary(GetOrParseIndirectObject(m_dwFirstPageObjNum))) {
      return pDict;
    }
  }

  int objnum = m_PageList.GetAt(iPage);
  if (objnum) {
    if (CPDF_Dictionary* pDict = ToDictionary(GetOrParseIndirectObject(objnum)))
      return pDict;
  }

  CPDF_Dictionary* pPages = GetPagesDict();
  if (!pPages)
    return nullptr;

  CPDF_Dictionary* pPage = FindPDFPage(pPages, iPage, iPage, 0);
  if (!pPage)
    return nullptr;

  m_PageList.SetAt(iPage, pPage->GetObjNum());
  return pPage;
}

void CPDF_Document::SetPageObjNum(int iPage, uint32_t objNum) {
  m_PageList.SetAt(iPage, objNum);
}

int CPDF_Document::FindPageIndex(CPDF_Dictionary* pNode,
                                 uint32_t& skip_count,
                                 uint32_t objnum,
                                 int& index,
                                 int level) {
  if (!pNode->KeyExist("Kids")) {
    if (objnum == pNode->GetObjNum())
      return index;

    if (skip_count)
      skip_count--;

    index++;
    return -1;
  }

  CPDF_Array* pKidList = pNode->GetArrayFor("Kids");
  if (!pKidList)
    return -1;

  if (level >= FX_MAX_PAGE_LEVEL)
    return -1;

  size_t count = pNode->GetIntegerFor("Count");
  if (count <= skip_count) {
    skip_count -= count;
    index += count;
    return -1;
  }

  if (count && count == pKidList->GetCount()) {
    for (size_t i = 0; i < count; i++) {
      if (CPDF_Reference* pKid = ToReference(pKidList->GetObjectAt(i))) {
        if (pKid->GetRefObjNum() == objnum) {
          m_PageList.SetAt(index + i, objnum);
          return static_cast<int>(index + i);
        }
      }
    }
  }

  for (size_t i = 0; i < pKidList->GetCount(); i++) {
    CPDF_Dictionary* pKid = pKidList->GetDictAt(i);
    if (!pKid || pKid == pNode)
      continue;

    int found_index = FindPageIndex(pKid, skip_count, objnum, index, level + 1);
    if (found_index >= 0)
      return found_index;
  }
  return -1;
}

int CPDF_Document::GetPageIndex(uint32_t objnum) {
  uint32_t nPages = m_PageList.GetSize();
  uint32_t skip_count = 0;
  bool bSkipped = false;
  for (uint32_t i = 0; i < nPages; i++) {
    uint32_t objnum1 = m_PageList.GetAt(i);
    if (objnum1 == objnum)
      return i;

    if (!bSkipped && objnum1 == 0) {
      skip_count = i;
      bSkipped = true;
    }
  }
  CPDF_Dictionary* pPages = GetPagesDict();
  if (!pPages)
    return -1;

  int index = 0;
  return FindPageIndex(pPages, skip_count, objnum, index);
}

int CPDF_Document::GetPageCount() const {
  return m_PageList.GetSize();
}

int CPDF_Document::RetrievePageCount() const {
  CPDF_Dictionary* pPages = GetPagesDict();
  if (!pPages)
    return 0;

  if (!pPages->KeyExist("Kids"))
    return 1;

  std::set<CPDF_Dictionary*> visited_pages;
  visited_pages.insert(pPages);
  return CountPages(pPages, &visited_pages);
}

uint32_t CPDF_Document::GetUserPermissions() const {
  // https://bugs.chromium.org/p/pdfium/issues/detail?id=499
  if (!m_pParser) {
#ifndef PDF_ENABLE_XFA
    return 0;
#else  // PDF_ENABLE_XFA
    return 0xFFFFFFFF;
#endif
  }
  return m_pParser->GetPermissions();
}

CPDF_Font* CPDF_Document::LoadFont(CPDF_Dictionary* pFontDict) {
  ASSERT(pFontDict);
  return m_pDocPage->GetFont(pFontDict);
}

CPDF_StreamAcc* CPDF_Document::LoadFontFile(CPDF_Stream* pStream) {
  return m_pDocPage->GetFontFileStreamAcc(pStream);
}

CPDF_ColorSpace* CPDF_Document::LoadColorSpace(CPDF_Object* pCSObj,
                                               CPDF_Dictionary* pResources) {
  return m_pDocPage->GetColorSpace(pCSObj, pResources);
}

CPDF_Pattern* CPDF_Document::LoadPattern(CPDF_Object* pPatternObj,
                                         bool bShading,
                                         const CFX_Matrix& matrix) {
  return m_pDocPage->GetPattern(pPatternObj, bShading, matrix);
}

CPDF_IccProfile* CPDF_Document::LoadIccProfile(CPDF_Stream* pStream) {
  return m_pDocPage->GetIccProfile(pStream);
}

CPDF_Image* CPDF_Document::LoadImageF(CPDF_Object* pObj) {
  if (!pObj)
    return nullptr;

  ASSERT(pObj->GetObjNum());
  return m_pDocPage->GetImage(pObj);
}

void CPDF_Document::CreateNewDoc() {
  ASSERT(!m_pRootDict && !m_pInfoDict);
  m_pRootDict = new CPDF_Dictionary(m_pByteStringPool);
  m_pRootDict->SetNameFor("Type", "Catalog");
  AddIndirectObject(m_pRootDict);

  CPDF_Dictionary* pPages = new CPDF_Dictionary(m_pByteStringPool);
  pPages->SetNameFor("Type", "Pages");
  pPages->SetNumberFor("Count", 0);
  pPages->SetFor("Kids", new CPDF_Array);
  m_pRootDict->SetReferenceFor("Pages", this, AddIndirectObject(pPages));
  m_pInfoDict = new CPDF_Dictionary(m_pByteStringPool);
  AddIndirectObject(m_pInfoDict);
}

CPDF_Dictionary* CPDF_Document::CreateNewPage(int iPage) {
  CPDF_Dictionary* pDict = new CPDF_Dictionary(m_pByteStringPool);
  pDict->SetNameFor("Type", "Page");
  uint32_t dwObjNum = AddIndirectObject(pDict);
  if (InsertNewPage(this, iPage, pDict, m_PageList) < 0) {
    ReleaseIndirectObject(dwObjNum);
    return nullptr;
  }
  return pDict;
}

void CPDF_Document::DeletePage(int iPage) {
  CPDF_Dictionary* pPages = GetPagesDict();
  if (!pPages)
    return;

  int nPages = pPages->GetIntegerFor("Count");
  if (iPage < 0 || iPage >= nPages)
    return;

  std::set<CPDF_Dictionary*> stack = {pPages};
  if (InsertDeletePDFPage(this, pPages, iPage, nullptr, FALSE, &stack) < 0)
    return;

  m_PageList.RemoveAt(iPage);
}

CPDF_Font* CPDF_Document::AddStandardFont(const FX_CHAR* font,
                                          CPDF_FontEncoding* pEncoding) {
  CFX_ByteString name(font);
  if (PDF_GetStandardFontName(&name) < 0)
    return nullptr;
  return GetPageData()->GetStandardFont(name, pEncoding);
}

size_t CPDF_Document::CalculateEncodingDict(int charset,
                                            CPDF_Dictionary* pBaseDict) {
  size_t i;
  for (i = 0; i < FX_ArraySize(g_FX_CharsetUnicodes); ++i) {
    if (g_FX_CharsetUnicodes[i].m_Charset == charset)
      break;
  }
  if (i == FX_ArraySize(g_FX_CharsetUnicodes))
    return i;
  CPDF_Dictionary* pEncodingDict = new CPDF_Dictionary(m_pByteStringPool);
  pEncodingDict->SetNameFor("BaseEncoding", "WinAnsiEncoding");
  CPDF_Array* pArray = new CPDF_Array;
  pArray->AddInteger(128);
  const uint16_t* pUnicodes = g_FX_CharsetUnicodes[i].m_pUnicodes;
  for (int j = 0; j < 128; j++) {
    CFX_ByteString name = PDF_AdobeNameFromUnicode(pUnicodes[j]);
    pArray->AddName(name.IsEmpty() ? ".notdef" : name);
  }
  pEncodingDict->SetFor("Differences", pArray);
  pBaseDict->SetReferenceFor("Encoding", this,
                             AddIndirectObject(pEncodingDict));

  return i;
}

CPDF_Dictionary* CPDF_Document::ProcessbCJK(
    CPDF_Dictionary* pBaseDict,
    int charset,
    FX_BOOL bVert,
    CFX_ByteString basefont,
    std::function<void(FX_WCHAR, FX_WCHAR, CPDF_Array*)> Insert) {
  CPDF_Dictionary* pFontDict = new CPDF_Dictionary(m_pByteStringPool);
  CFX_ByteString cmap;
  CFX_ByteString ordering;
  int supplement = 0;
  CPDF_Array* pWidthArray = new CPDF_Array;
  switch (charset) {
    case FXFONT_CHINESEBIG5_CHARSET:
      cmap = bVert ? "ETenms-B5-V" : "ETenms-B5-H";
      ordering = "CNS1";
      supplement = 4;
      pWidthArray->AddInteger(1);
      Insert(0x20, 0x7e, pWidthArray);
      break;
    case FXFONT_GB2312_CHARSET:
      cmap = bVert ? "GBK-EUC-V" : "GBK-EUC-H";
      ordering = "GB1";
      supplement = 2;
      pWidthArray->AddInteger(7716);
      Insert(0x20, 0x20, pWidthArray);
      pWidthArray->AddInteger(814);
      Insert(0x21, 0x7e, pWidthArray);
      break;
    case FXFONT_HANGUL_CHARSET:
      cmap = bVert ? "KSCms-UHC-V" : "KSCms-UHC-H";
      ordering = "Korea1";
      supplement = 2;
      pWidthArray->AddInteger(1);
      Insert(0x20, 0x7e, pWidthArray);
      break;
    case FXFONT_SHIFTJIS_CHARSET:
      cmap = bVert ? "90ms-RKSJ-V" : "90ms-RKSJ-H";
      ordering = "Japan1";
      supplement = 5;
      pWidthArray->AddInteger(231);
      Insert(0x20, 0x7d, pWidthArray);
      pWidthArray->AddInteger(326);
      Insert(0xa0, 0xa0, pWidthArray);
      pWidthArray->AddInteger(327);
      Insert(0xa1, 0xdf, pWidthArray);
      pWidthArray->AddInteger(631);
      Insert(0x7e, 0x7e, pWidthArray);
      break;
  }
  pBaseDict->SetNameFor("Subtype", "Type0");
  pBaseDict->SetNameFor("BaseFont", basefont);
  pBaseDict->SetNameFor("Encoding", cmap);
  pFontDict->SetFor("W", pWidthArray);
  pFontDict->SetNameFor("Type", "Font");
  pFontDict->SetNameFor("Subtype", "CIDFontType2");
  pFontDict->SetNameFor("BaseFont", basefont);
  CPDF_Dictionary* pCIDSysInfo = new CPDF_Dictionary(m_pByteStringPool);
  pCIDSysInfo->SetStringFor("Registry", "Adobe");
  pCIDSysInfo->SetStringFor("Ordering", ordering);
  pCIDSysInfo->SetIntegerFor("Supplement", supplement);
  pFontDict->SetFor("CIDSystemInfo", pCIDSysInfo);
  CPDF_Array* pArray = new CPDF_Array;
  pBaseDict->SetFor("DescendantFonts", pArray);
  pArray->AddReference(this, AddIndirectObject(pFontDict));
  return pFontDict;
}

CPDF_Font* CPDF_Document::AddFont(CFX_Font* pFont, int charset, FX_BOOL bVert) {
  if (!pFont)
    return nullptr;

  bool bCJK = charset == FXFONT_CHINESEBIG5_CHARSET ||
              charset == FXFONT_GB2312_CHARSET ||
              charset == FXFONT_HANGUL_CHARSET ||
              charset == FXFONT_SHIFTJIS_CHARSET;
  CFX_ByteString basefont = pFont->GetFamilyName();
  basefont.Replace(" ", "");
  int flags =
      CalculateFlags(pFont->IsBold(), pFont->IsItalic(), pFont->IsFixedWidth(),
                     false, false, charset == FXFONT_SYMBOL_CHARSET);

  CPDF_Dictionary* pBaseDict = new CPDF_Dictionary(m_pByteStringPool);
  pBaseDict->SetNameFor("Type", "Font");
  std::unique_ptr<CFX_UnicodeEncoding> pEncoding(
      new CFX_UnicodeEncoding(pFont));
  CPDF_Dictionary* pFontDict = pBaseDict;
  if (!bCJK) {
    CPDF_Array* pWidths = new CPDF_Array;
    for (int charcode = 32; charcode < 128; charcode++) {
      int glyph_index = pEncoding->GlyphFromCharCode(charcode);
      int char_width = pFont->GetGlyphWidth(glyph_index);
      pWidths->AddInteger(char_width);
    }
    if (charset == FXFONT_ANSI_CHARSET || charset == FXFONT_DEFAULT_CHARSET ||
        charset == FXFONT_SYMBOL_CHARSET) {
      pBaseDict->SetNameFor("Encoding", "WinAnsiEncoding");
      for (int charcode = 128; charcode <= 255; charcode++) {
        int glyph_index = pEncoding->GlyphFromCharCode(charcode);
        int char_width = pFont->GetGlyphWidth(glyph_index);
        pWidths->AddInteger(char_width);
      }
    } else {
      size_t i = CalculateEncodingDict(charset, pBaseDict);
      if (i < FX_ArraySize(g_FX_CharsetUnicodes)) {
        const uint16_t* pUnicodes = g_FX_CharsetUnicodes[i].m_pUnicodes;
        for (int j = 0; j < 128; j++) {
          int glyph_index = pEncoding->GlyphFromCharCode(pUnicodes[j]);
          int char_width = pFont->GetGlyphWidth(glyph_index);
          pWidths->AddInteger(char_width);
        }
      }
    }
    ProcessNonbCJK(pBaseDict, pFont->IsBold(), pFont->IsItalic(), basefont,
                   pWidths);
  } else {
    pFontDict = ProcessbCJK(pBaseDict, charset, bVert, basefont,
                            [pFont, &pEncoding](FX_WCHAR start, FX_WCHAR end,
                                                CPDF_Array* widthArr) {
                              InsertWidthArray1(pFont, pEncoding.get(), start,
                                                end, widthArr);
                            });
  }
  AddIndirectObject(pBaseDict);
  int italicangle =
      pFont->GetSubstFont() ? pFont->GetSubstFont()->m_ItalicAngle : 0;
  FX_RECT bbox;
  pFont->GetBBox(bbox);
  CPDF_Array* pBBox = new CPDF_Array;
  pBBox->AddInteger(bbox.left);
  pBBox->AddInteger(bbox.bottom);
  pBBox->AddInteger(bbox.right);
  pBBox->AddInteger(bbox.top);
  int32_t nStemV = 0;
  if (pFont->GetSubstFont()) {
    nStemV = pFont->GetSubstFont()->m_Weight / 5;
  } else {
    static const FX_CHAR stem_chars[] = {'i', 'I', '!', '1'};
    const size_t count = FX_ArraySize(stem_chars);
    uint32_t glyph = pEncoding->GlyphFromCharCode(stem_chars[0]);
    nStemV = pFont->GetGlyphWidth(glyph);
    for (size_t i = 1; i < count; i++) {
      glyph = pEncoding->GlyphFromCharCode(stem_chars[i]);
      int width = pFont->GetGlyphWidth(glyph);
      if (width > 0 && width < nStemV)
        nStemV = width;
    }
  }
  CPDF_Dictionary* pFontDesc =
      CalculateFontDesc(this, basefont, flags, italicangle, pFont->GetAscent(),
                        pFont->GetDescent(), pBBox, nStemV);
  pFontDict->SetReferenceFor("FontDescriptor", this,
                             AddIndirectObject(pFontDesc));
  return LoadFont(pBaseDict);
}

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
CPDF_Font* CPDF_Document::AddWindowsFont(LOGFONTW* pLogFont,
                                         FX_BOOL bVert,
                                         FX_BOOL bTranslateName) {
  LOGFONTA lfa;
  FXSYS_memcpy(&lfa, pLogFont, (char*)lfa.lfFaceName - (char*)&lfa);
  CFX_ByteString face = CFX_ByteString::FromUnicode(pLogFont->lfFaceName);
  if (face.GetLength() >= LF_FACESIZE)
    return nullptr;

  FXSYS_strcpy(lfa.lfFaceName, face.c_str());
  return AddWindowsFont(&lfa, bVert, bTranslateName);
}

CPDF_Font* CPDF_Document::AddWindowsFont(LOGFONTA* pLogFont,
                                         FX_BOOL bVert,
                                         FX_BOOL bTranslateName) {
  pLogFont->lfHeight = -1000;
  pLogFont->lfWidth = 0;
  HGDIOBJ hFont = CreateFontIndirectA(pLogFont);
  HDC hDC = CreateCompatibleDC(nullptr);
  hFont = SelectObject(hDC, hFont);
  int tm_size = GetOutlineTextMetrics(hDC, 0, nullptr);
  if (tm_size == 0) {
    hFont = SelectObject(hDC, hFont);
    DeleteObject(hFont);
    DeleteDC(hDC);
    return nullptr;
  }

  LPBYTE tm_buf = FX_Alloc(BYTE, tm_size);
  OUTLINETEXTMETRIC* ptm = reinterpret_cast<OUTLINETEXTMETRIC*>(tm_buf);
  GetOutlineTextMetrics(hDC, tm_size, ptm);
  int flags = CalculateFlags(false, pLogFont->lfItalic != 0,
                             (pLogFont->lfPitchAndFamily & 3) == FIXED_PITCH,
                             (pLogFont->lfPitchAndFamily & 0xf8) == FF_ROMAN,
                             (pLogFont->lfPitchAndFamily & 0xf8) == FF_SCRIPT,
                             pLogFont->lfCharSet == FXFONT_SYMBOL_CHARSET);

  bool bCJK = pLogFont->lfCharSet == FXFONT_CHINESEBIG5_CHARSET ||
              pLogFont->lfCharSet == FXFONT_GB2312_CHARSET ||
              pLogFont->lfCharSet == FXFONT_HANGUL_CHARSET ||
              pLogFont->lfCharSet == FXFONT_SHIFTJIS_CHARSET;
  CFX_ByteString basefont;
  if (bTranslateName && bCJK)
    basefont = FPDF_GetPSNameFromTT(hDC);

  if (basefont.IsEmpty())
    basefont = pLogFont->lfFaceName;

  int italicangle = ptm->otmItalicAngle / 10;
  int ascend = ptm->otmrcFontBox.top;
  int descend = ptm->otmrcFontBox.bottom;
  int capheight = ptm->otmsCapEmHeight;
  int bbox[4] = {ptm->otmrcFontBox.left, ptm->otmrcFontBox.bottom,
                 ptm->otmrcFontBox.right, ptm->otmrcFontBox.top};
  FX_Free(tm_buf);
  basefont.Replace(" ", "");
  CPDF_Dictionary* pBaseDict = new CPDF_Dictionary(m_pByteStringPool);
  pBaseDict->SetNameFor("Type", "Font");
  CPDF_Dictionary* pFontDict = pBaseDict;
  if (!bCJK) {
    if (pLogFont->lfCharSet == FXFONT_ANSI_CHARSET ||
        pLogFont->lfCharSet == FXFONT_DEFAULT_CHARSET ||
        pLogFont->lfCharSet == FXFONT_SYMBOL_CHARSET) {
      pBaseDict->SetNameFor("Encoding", "WinAnsiEncoding");
    } else {
      CalculateEncodingDict(pLogFont->lfCharSet, pBaseDict);
    }
    int char_widths[224];
    GetCharWidth(hDC, 32, 255, char_widths);
    CPDF_Array* pWidths = new CPDF_Array;
    for (size_t i = 0; i < 224; i++)
      pWidths->AddInteger(char_widths[i]);
    ProcessNonbCJK(pBaseDict, pLogFont->lfWeight > FW_MEDIUM,
                   pLogFont->lfItalic != 0, basefont, pWidths);
  } else {
    pFontDict =
        ProcessbCJK(pBaseDict, pLogFont->lfCharSet, bVert, basefont,
                    [&hDC](FX_WCHAR start, FX_WCHAR end, CPDF_Array* widthArr) {
                      InsertWidthArray(hDC, start, end, widthArr);
                    });
  }
  AddIndirectObject(pBaseDict);
  CPDF_Array* pBBox = new CPDF_Array;
  for (int i = 0; i < 4; i++)
    pBBox->AddInteger(bbox[i]);
  CPDF_Dictionary* pFontDesc =
      CalculateFontDesc(this, basefont, flags, italicangle, ascend, descend,
                        pBBox, pLogFont->lfWeight / 5);
  pFontDesc->SetIntegerFor("CapHeight", capheight);
  pFontDict->SetReferenceFor("FontDescriptor", this,
                             AddIndirectObject(pFontDesc));
  hFont = SelectObject(hDC, hFont);
  DeleteObject(hFont);
  DeleteDC(hDC);
  return LoadFont(pBaseDict);
}
#endif  //  _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
