/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "platform/graphics/filters/FEComponentTransfer.h"

#include "SkColorFilterImageFilter.h"
#include "SkTableColorFilter.h"
#include "platform/graphics/filters/SkiaImageFilterBuilder.h"
#include "platform/text/TextStream.h"
#include "wtf/MathExtras.h"
#include <algorithm>

namespace blink {

typedef void (*TransferType)(unsigned char*, const ComponentTransferFunction&);

FEComponentTransfer::FEComponentTransfer(
    Filter* filter,
    const ComponentTransferFunction& redFunc,
    const ComponentTransferFunction& greenFunc,
    const ComponentTransferFunction& blueFunc,
    const ComponentTransferFunction& alphaFunc)
    : FilterEffect(filter),
      m_redFunc(redFunc),
      m_greenFunc(greenFunc),
      m_blueFunc(blueFunc),
      m_alphaFunc(alphaFunc) {}

FEComponentTransfer* FEComponentTransfer::create(
    Filter* filter,
    const ComponentTransferFunction& redFunc,
    const ComponentTransferFunction& greenFunc,
    const ComponentTransferFunction& blueFunc,
    const ComponentTransferFunction& alphaFunc) {
  return new FEComponentTransfer(filter, redFunc, greenFunc, blueFunc,
                                 alphaFunc);
}

static void identity(unsigned char*, const ComponentTransferFunction&) {}

static void table(unsigned char* values,
                  const ComponentTransferFunction& transferFunction) {
  const Vector<float>& tableValues = transferFunction.tableValues;
  unsigned n = tableValues.size();
  if (n < 1)
    return;
  for (unsigned i = 0; i < 256; ++i) {
    double c = i / 255.0;
    unsigned k = static_cast<unsigned>(c * (n - 1));
    double v1 = tableValues[k];
    double v2 = tableValues[std::min((k + 1), (n - 1))];
    double val = 255.0 * (v1 + (c * (n - 1) - k) * (v2 - v1));
    val = clampTo(val, 0.0, 255.0);
    values[i] = static_cast<unsigned char>(val);
  }
}

static void discrete(unsigned char* values,
                     const ComponentTransferFunction& transferFunction) {
  const Vector<float>& tableValues = transferFunction.tableValues;
  unsigned n = tableValues.size();
  if (n < 1)
    return;
  for (unsigned i = 0; i < 256; ++i) {
    unsigned k = static_cast<unsigned>((i * n) / 255.0);
    k = std::min(k, n - 1);
    double val = 255 * tableValues[k];
    val = clampTo(val, 0.0, 255.0);
    values[i] = static_cast<unsigned char>(val);
  }
}

static void linear(unsigned char* values,
                   const ComponentTransferFunction& transferFunction) {
  for (unsigned i = 0; i < 256; ++i) {
    double val = transferFunction.slope * i + 255 * transferFunction.intercept;
    val = clampTo(val, 0.0, 255.0);
    values[i] = static_cast<unsigned char>(val);
  }
}

static void gamma(unsigned char* values,
                  const ComponentTransferFunction& transferFunction) {
  for (unsigned i = 0; i < 256; ++i) {
    double exponent = transferFunction.exponent;
    double val =
        255.0 * (transferFunction.amplitude * pow((i / 255.0), exponent) +
                 transferFunction.offset);
    val = clampTo(val, 0.0, 255.0);
    values[i] = static_cast<unsigned char>(val);
  }
}

bool FEComponentTransfer::affectsTransparentPixels() const {
  double intercept = 0;
  switch (m_alphaFunc.type) {
    case FECOMPONENTTRANSFER_TYPE_UNKNOWN:
    case FECOMPONENTTRANSFER_TYPE_IDENTITY:
      break;
    case FECOMPONENTTRANSFER_TYPE_TABLE:
    case FECOMPONENTTRANSFER_TYPE_DISCRETE:
      if (m_alphaFunc.tableValues.size() > 0)
        intercept = m_alphaFunc.tableValues[0];
      break;
    case FECOMPONENTTRANSFER_TYPE_LINEAR:
      intercept = m_alphaFunc.intercept;
      break;
    case FECOMPONENTTRANSFER_TYPE_GAMMA:
      intercept = m_alphaFunc.offset;
      break;
  }
  return 255 * intercept >= 1;
}

sk_sp<SkImageFilter> FEComponentTransfer::createImageFilter() {
  sk_sp<SkImageFilter> input(
      SkiaImageFilterBuilder::build(inputEffect(0), operatingColorSpace()));

  unsigned char rValues[256], gValues[256], bValues[256], aValues[256];
  getValues(rValues, gValues, bValues, aValues);

  SkImageFilter::CropRect cropRect = getCropRect();
  sk_sp<SkColorFilter> colorFilter =
      SkTableColorFilter::MakeARGB(aValues, rValues, gValues, bValues);
  return SkColorFilterImageFilter::Make(std::move(colorFilter),
                                        std::move(input), &cropRect);
}

void FEComponentTransfer::getValues(unsigned char rValues[256],
                                    unsigned char gValues[256],
                                    unsigned char bValues[256],
                                    unsigned char aValues[256]) {
  for (unsigned i = 0; i < 256; ++i)
    rValues[i] = gValues[i] = bValues[i] = aValues[i] = i;
  unsigned char* tables[] = {rValues, gValues, bValues, aValues};
  ComponentTransferFunction transferFunction[] = {m_redFunc, m_greenFunc,
                                                  m_blueFunc, m_alphaFunc};
  TransferType callEffect[] = {identity, identity, table,
                               discrete, linear,   gamma};

  for (unsigned channel = 0; channel < 4; channel++) {
    ASSERT_WITH_SECURITY_IMPLICATION(
        static_cast<size_t>(transferFunction[channel].type) <
        WTF_ARRAY_LENGTH(callEffect));
    (*callEffect[transferFunction[channel].type])(tables[channel],
                                                  transferFunction[channel]);
  }
}

static TextStream& operator<<(TextStream& ts,
                              const ComponentTransferType& type) {
  switch (type) {
    case FECOMPONENTTRANSFER_TYPE_UNKNOWN:
      ts << "UNKNOWN";
      break;
    case FECOMPONENTTRANSFER_TYPE_IDENTITY:
      ts << "IDENTITY";
      break;
    case FECOMPONENTTRANSFER_TYPE_TABLE:
      ts << "TABLE";
      break;
    case FECOMPONENTTRANSFER_TYPE_DISCRETE:
      ts << "DISCRETE";
      break;
    case FECOMPONENTTRANSFER_TYPE_LINEAR:
      ts << "LINEAR";
      break;
    case FECOMPONENTTRANSFER_TYPE_GAMMA:
      ts << "GAMMA";
      break;
  }
  return ts;
}

static TextStream& operator<<(TextStream& ts,
                              const ComponentTransferFunction& function) {
  ts << "type=\"" << function.type << "\" slope=\"" << function.slope
     << "\" intercept=\"" << function.intercept << "\" amplitude=\""
     << function.amplitude << "\" exponent=\"" << function.exponent
     << "\" offset=\"" << function.offset << "\"";
  return ts;
}

TextStream& FEComponentTransfer::externalRepresentation(TextStream& ts,
                                                        int indent) const {
  writeIndent(ts, indent);
  ts << "[feComponentTransfer";
  FilterEffect::externalRepresentation(ts);
  ts << " \n";
  writeIndent(ts, indent + 2);
  ts << "{red: " << m_redFunc << "}\n";
  writeIndent(ts, indent + 2);
  ts << "{green: " << m_greenFunc << "}\n";
  writeIndent(ts, indent + 2);
  ts << "{blue: " << m_blueFunc << "}\n";
  writeIndent(ts, indent + 2);
  ts << "{alpha: " << m_alphaFunc << "}]\n";
  inputEffect(0)->externalRepresentation(ts, indent + 1);
  return ts;
}

}  // namespace blink
