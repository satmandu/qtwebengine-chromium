// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/animation/CSSFontWeightInterpolationType.h"

#include <memory>
#include "core/animation/FontWeightConversion.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/resolver/StyleResolverState.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

class InheritedFontWeightChecker : public InterpolationType::ConversionChecker {
 public:
  static std::unique_ptr<InheritedFontWeightChecker> Create(
      FontWeight font_weight) {
    return WTF::WrapUnique(new InheritedFontWeightChecker(font_weight));
  }

 private:
  InheritedFontWeightChecker(FontWeight font_weight)
      : font_weight_(font_weight) {}

  bool IsValid(const InterpolationEnvironment& environment,
               const InterpolationValue&) const final {
    return font_weight_ ==
           environment.GetState().ParentStyle()->GetFontWeight();
  }

  const double font_weight_;
};

InterpolationValue CSSFontWeightInterpolationType::CreateFontWeightValue(
    FontWeight font_weight) const {
  return InterpolationValue(
      InterpolableNumber::Create(FontWeightToDouble(font_weight)));
}

InterpolationValue CSSFontWeightInterpolationType::MaybeConvertNeutral(
    const InterpolationValue&,
    ConversionCheckers&) const {
  return InterpolationValue(InterpolableNumber::Create(0));
}

InterpolationValue CSSFontWeightInterpolationType::MaybeConvertInitial(
    const StyleResolverState&,
    ConversionCheckers& conversion_checkers) const {
  return CreateFontWeightValue(kFontWeightNormal);
}

InterpolationValue CSSFontWeightInterpolationType::MaybeConvertInherit(
    const StyleResolverState& state,
    ConversionCheckers& conversion_checkers) const {
  if (!state.ParentStyle())
    return nullptr;
  FontWeight inherited_font_weight = state.ParentStyle()->GetFontWeight();
  conversion_checkers.push_back(
      InheritedFontWeightChecker::Create(inherited_font_weight));
  return CreateFontWeightValue(inherited_font_weight);
}

InterpolationValue CSSFontWeightInterpolationType::MaybeConvertValue(
    const CSSValue& value,
    const StyleResolverState* state,
    ConversionCheckers& conversion_checkers) const {
  if (!value.IsIdentifierValue())
    return nullptr;

  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
  CSSValueID keyword = identifier_value.GetValueID();

  switch (keyword) {
    case CSSValueInvalid:
      return nullptr;

    case CSSValueBolder:
    case CSSValueLighter: {
      DCHECK(state);
      FontWeight inherited_font_weight = state->ParentStyle()->GetFontWeight();
      conversion_checkers.push_back(
          InheritedFontWeightChecker::Create(inherited_font_weight));
      if (keyword == CSSValueBolder)
        return CreateFontWeightValue(
            FontDescription::BolderWeight(inherited_font_weight));
      return CreateFontWeightValue(
          FontDescription::LighterWeight(inherited_font_weight));
    }

    default:
      return CreateFontWeightValue(identifier_value.ConvertTo<FontWeight>());
  }
}

InterpolationValue
CSSFontWeightInterpolationType::MaybeConvertStandardPropertyUnderlyingValue(
    const ComputedStyle& style) const {
  return CreateFontWeightValue(style.GetFontWeight());
}

void CSSFontWeightInterpolationType::ApplyStandardPropertyValue(
    const InterpolableValue& interpolable_value,
    const NonInterpolableValue*,
    StyleResolverState& state) const {
  state.GetFontBuilder().SetWeight(
      DoubleToFontWeight(ToInterpolableNumber(interpolable_value).Value()));
}

}  // namespace blink
