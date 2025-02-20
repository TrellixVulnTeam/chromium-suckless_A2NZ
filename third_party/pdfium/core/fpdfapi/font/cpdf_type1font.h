// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_FONT_CPDF_TYPE1FONT_H_
#define CORE_FPDFAPI_FONT_CPDF_TYPE1FONT_H_

#include "core/fpdfapi/font/cpdf_simplefont.h"
#include "core/fxcrt/fx_system.h"

class CPDF_Type1Font : public CPDF_SimpleFont {
 public:
  CPDF_Type1Font();

  // CPDF_Font:
  bool IsType1Font() const override;
  const CPDF_Type1Font* AsType1Font() const override;
  CPDF_Type1Font* AsType1Font() override;
  int GlyphFromCharCodeExt(uint32_t charcode) override;

  int GetBase14Font() const { return m_Base14Font; }

 protected:
  // CPDF_Font:
  bool Load() override;

  // CPDF_SimpleFont:
  void LoadGlyphMap() override;

  int m_Base14Font;
};

#endif  // CORE_FPDFAPI_FONT_CPDF_TYPE1FONT_H_
