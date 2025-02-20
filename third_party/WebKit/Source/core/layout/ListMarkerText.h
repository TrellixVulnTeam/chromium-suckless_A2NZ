/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2009 Apple Inc. All rights reserved.
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
 *
 */

#ifndef ListMarkerText_h
#define ListMarkerText_h

#include "core/style/ComputedStyleConstants.h"
#include "wtf/text/Unicode.h"
#include "wtf/text/WTFString.h"

namespace blink {

// Utility routines for working with lists. Encapsulates complex
// language-specific logic including fallback to alternate languages when
// counter values cannot be represented in a particular language.
namespace ListMarkerText {

// Returns the suffix character, such as '.', for the given list type and
// item count number.
UChar suffix(EListStyleType, int count);

// Returns the text, such as arabic or roman numerals, for the given list
// type and item count number. Does not include any suffix character.
String text(EListStyleType, int count);

}  // namespace ListMarkerText

}  // namespace blink

#endif  // ListMarkerText_h
