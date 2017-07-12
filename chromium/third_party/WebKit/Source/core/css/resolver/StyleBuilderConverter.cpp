/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/css/resolver/StyleBuilderConverter.h"

#include "core/css/BasicShapeFunctions.h"
#include "core/css/CSSBasicShapeValues.h"
#include "core/css/CSSColorValue.h"
#include "core/css/CSSContentDistributionValue.h"
#include "core/css/CSSCustomIdentValue.h"
#include "core/css/CSSFontFamilyValue.h"
#include "core/css/CSSFontFeatureValue.h"
#include "core/css/CSSFontVariationValue.h"
#include "core/css/CSSFunctionValue.h"
#include "core/css/CSSGridAutoRepeatValue.h"
#include "core/css/CSSGridLineNamesValue.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSPathValue.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/css/CSSQuadValue.h"
#include "core/css/CSSReflectValue.h"
#include "core/css/CSSShadowValue.h"
#include "core/css/CSSStringValue.h"
#include "core/css/CSSURIValue.h"
#include "core/css/CSSValuePair.h"
#include "core/css/resolver/FilterOperationResolver.h"
#include "core/css/resolver/TransformBuilder.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/UseCounter.h"
#include "core/style/ClipPathOperation.h"
#include "core/style/TextSizeAdjust.h"
#include "core/svg/SVGURIReference.h"
#include "platform/fonts/FontCache.h"
#include "platform/transforms/RotateTransformOperation.h"
#include "platform/transforms/ScaleTransformOperation.h"
#include "platform/transforms/TranslateTransformOperation.h"
#include <algorithm>

namespace blink {

using namespace cssvalue;

namespace {

static GridLength ConvertGridTrackBreadth(const StyleResolverState& state,
                                          const CSSValue& value) {
  // Fractional unit.
  if (value.IsPrimitiveValue() && ToCSSPrimitiveValue(value).IsFlex())
    return GridLength(ToCSSPrimitiveValue(value).GetDoubleValue());

  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueMinContent)
    return Length(kMinContent);

  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueMaxContent)
    return Length(kMaxContent);

  return StyleBuilderConverter::ConvertLengthOrAuto(state, value);
}

}  // namespace

PassRefPtr<StyleReflection> StyleBuilderConverter::ConvertBoxReflect(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return ComputedStyle::InitialBoxReflect();
  }

  const CSSReflectValue& reflect_value = ToCSSReflectValue(value);
  RefPtr<StyleReflection> reflection = StyleReflection::Create();
  reflection->SetDirection(
      reflect_value.Direction()->ConvertTo<CSSReflectionDirection>());
  if (reflect_value.Offset())
    reflection->SetOffset(reflect_value.Offset()->ConvertToLength(
        state.CssToLengthConversionData()));
  if (reflect_value.Mask()) {
    NinePieceImage mask;
    mask.SetMaskDefaults();
    CSSToStyleMap::MapNinePieceImage(state, CSSPropertyWebkitBoxReflect,
                                     *reflect_value.Mask(), mask);
    reflection->SetMask(mask);
  }

  return reflection.Release();
}

Color StyleBuilderConverter::ConvertColor(StyleResolverState& state,
                                          const CSSValue& value,
                                          bool for_visited_link) {
  return state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
      value, state.Style()->GetColor(), for_visited_link);
}

AtomicString StyleBuilderConverter::ConvertFragmentIdentifier(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsURIValue())
    return SVGURIReference::FragmentIdentifierFromIRIString(
        ToCSSURIValue(value).Value(), state.GetElement()->GetTreeScope());
  return g_null_atom;
}

LengthBox StyleBuilderConverter::ConvertClip(StyleResolverState& state,
                                             const CSSValue& value) {
  const CSSQuadValue& rect = ToCSSQuadValue(value);

  return LengthBox(ConvertLengthOrAuto(state, *rect.Top()),
                   ConvertLengthOrAuto(state, *rect.Right()),
                   ConvertLengthOrAuto(state, *rect.Bottom()),
                   ConvertLengthOrAuto(state, *rect.Left()));
}

PassRefPtr<ClipPathOperation> StyleBuilderConverter::ConvertClipPath(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsBasicShapeValue())
    return ShapeClipPathOperation::Create(BasicShapeForValue(state, value));
  if (value.IsURIValue()) {
    const CSSURIValue& url_value = ToCSSURIValue(value);
    SVGElementProxy& element_proxy =
        state.GetElementStyleResources().CachedOrPendingFromValue(url_value);
    // TODO(fs): Doesn't work with external SVG references (crbug.com/109212.)
    return ReferenceClipPathOperation::Create(url_value.Value(), element_proxy);
  }
  DCHECK(value.IsIdentifierValue() &&
         ToCSSIdentifierValue(value).GetValueID() == CSSValueNone);
  return nullptr;
}

FilterOperations StyleBuilderConverter::ConvertFilterOperations(
    StyleResolverState& state,
    const CSSValue& value) {
  return FilterOperationResolver::CreateFilterOperations(state, value);
}

FilterOperations StyleBuilderConverter::ConvertOffscreenFilterOperations(
    const CSSValue& value) {
  return FilterOperationResolver::CreateOffscreenFilterOperations(value);
}

static FontDescription::GenericFamilyType ConvertGenericFamily(
    CSSValueID value_id) {
  switch (value_id) {
    case CSSValueWebkitBody:
      return FontDescription::kStandardFamily;
    case CSSValueSerif:
      return FontDescription::kSerifFamily;
    case CSSValueSansSerif:
      return FontDescription::kSansSerifFamily;
    case CSSValueCursive:
      return FontDescription::kCursiveFamily;
    case CSSValueFantasy:
      return FontDescription::kFantasyFamily;
    case CSSValueMonospace:
      return FontDescription::kMonospaceFamily;
    case CSSValueWebkitPictograph:
      return FontDescription::kPictographFamily;
    default:
      return FontDescription::kNoFamily;
  }
}

static bool ConvertFontFamilyName(
    StyleResolverState& state,
    const CSSValue& value,
    FontDescription::GenericFamilyType& generic_family,
    AtomicString& family_name) {
  if (value.IsFontFamilyValue()) {
    generic_family = FontDescription::kNoFamily;
    family_name = AtomicString(ToCSSFontFamilyValue(value).Value());
#if OS(MACOSX)
    if (family_name == FontCache::LegacySystemFontFamily()) {
      UseCounter::Count(state.GetDocument(), UseCounter::kBlinkMacSystemFont);
      family_name = FontFamilyNames::system_ui;
    }
#endif
  } else if (state.GetDocument().GetSettings()) {
    generic_family =
        ConvertGenericFamily(ToCSSIdentifierValue(value).GetValueID());
    family_name = state.GetFontBuilder().GenericFontFamilyName(generic_family);
  }

  return !family_name.IsEmpty();
}

FontDescription::FamilyDescription StyleBuilderConverter::ConvertFontFamily(
    StyleResolverState& state,
    const CSSValue& value) {
  DCHECK(value.IsValueList());

  FontDescription::FamilyDescription desc(FontDescription::kNoFamily);
  FontFamily* curr_family = nullptr;

  for (auto& family : ToCSSValueList(value)) {
    FontDescription::GenericFamilyType generic_family =
        FontDescription::kNoFamily;
    AtomicString family_name;

    if (!ConvertFontFamilyName(state, *family, generic_family, family_name))
      continue;

    if (!curr_family) {
      curr_family = &desc.family;
    } else {
      RefPtr<SharedFontFamily> new_family = SharedFontFamily::Create();
      curr_family->AppendFamily(new_family);
      curr_family = new_family.Get();
    }

    curr_family->SetFamily(family_name);

    if (generic_family != FontDescription::kNoFamily)
      desc.generic_family = generic_family;
  }

  return desc;
}

PassRefPtr<FontFeatureSettings>
StyleBuilderConverter::ConvertFontFeatureSettings(StyleResolverState& state,
                                                  const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNormal)
    return FontBuilder::InitialFeatureSettings();

  const CSSValueList& list = ToCSSValueList(value);
  RefPtr<FontFeatureSettings> settings = FontFeatureSettings::Create();
  int len = list.length();
  for (int i = 0; i < len; ++i) {
    const CSSFontFeatureValue& feature = ToCSSFontFeatureValue(list.Item(i));
    settings->Append(FontFeature(feature.Tag(), feature.Value()));
  }
  return settings;
}

PassRefPtr<FontVariationSettings>
StyleBuilderConverter::ConvertFontVariationSettings(StyleResolverState& state,
                                                    const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNormal)
    return FontBuilder::InitialVariationSettings();

  const CSSValueList& list = ToCSSValueList(value);
  RefPtr<FontVariationSettings> settings = FontVariationSettings::Create();
  int len = list.length();
  for (int i = 0; i < len; ++i) {
    const CSSFontVariationValue& feature =
        ToCSSFontVariationValue(list.Item(i));
    settings->Append(FontVariationAxis(feature.Tag(), feature.Value()));
  }
  return settings;
}

static float ComputeFontSize(StyleResolverState& state,
                             const CSSPrimitiveValue& primitive_value,
                             const FontDescription::Size& parent_size) {
  if (primitive_value.IsLength())
    return primitive_value.ComputeLength<float>(state.FontSizeConversionData());
  if (primitive_value.IsCalculatedPercentageWithLength())
    return primitive_value.CssCalcValue()
        ->ToCalcValue(state.FontSizeConversionData())
        ->Evaluate(parent_size.value);

  NOTREACHED();
  return 0;
}

FontDescription::Size StyleBuilderConverter::ConvertFontSize(
    StyleResolverState& state,
    const CSSValue& value) {
  FontDescription::Size parent_size(0, 0.0f, false);

  // FIXME: Find out when parentStyle could be 0?
  if (state.ParentStyle())
    parent_size = state.ParentFontDescription().GetSize();

  if (value.IsIdentifierValue()) {
    CSSValueID value_id = ToCSSIdentifierValue(value).GetValueID();
    if (FontSize::IsValidValueID(value_id))
      return FontDescription::Size(FontSize::KeywordSize(value_id), 0.0f,
                                   false);
    if (value_id == CSSValueSmaller)
      return FontDescription::SmallerSize(parent_size);
    if (value_id == CSSValueLarger)
      return FontDescription::LargerSize(parent_size);
    NOTREACHED();
    return FontBuilder::InitialSize();
  }

  bool parent_is_absolute_size = state.ParentFontDescription().IsAbsoluteSize();

  const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
  if (primitive_value.IsPercentage())
    return FontDescription::Size(
        0, (primitive_value.GetFloatValue() * parent_size.value / 100.0f),
        parent_is_absolute_size);

  return FontDescription::Size(
      0, ComputeFontSize(state, primitive_value, parent_size),
      parent_is_absolute_size || !primitive_value.IsFontRelativeLength());
}

float StyleBuilderConverter::ConvertFontSizeAdjust(StyleResolverState& state,
                                                   const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNone)
    return FontBuilder::InitialSizeAdjust();

  const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
  DCHECK(primitive_value.IsNumber());
  return primitive_value.GetFloatValue();
}

double StyleBuilderConverter::ConvertValueToNumber(
    const CSSFunctionValue* filter,
    const CSSPrimitiveValue* value) {
  switch (filter->FunctionType()) {
    case CSSValueGrayscale:
    case CSSValueSepia:
    case CSSValueSaturate:
    case CSSValueInvert:
    case CSSValueBrightness:
    case CSSValueContrast:
    case CSSValueOpacity: {
      double amount = (filter->FunctionType() == CSSValueBrightness) ? 0 : 1;
      if (filter->length() == 1) {
        amount = value->GetDoubleValue();
        if (value->IsPercentage())
          amount /= 100;
      }
      return amount;
    }
    case CSSValueHueRotate: {
      double angle = 0;
      if (filter->length() == 1)
        angle = value->ComputeDegrees();
      return angle;
    }
    default:
      return 0;
  }
}

FontWeight StyleBuilderConverter::ConvertFontWeight(StyleResolverState& state,
                                                    const CSSValue& value) {
  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
  switch (identifier_value.GetValueID()) {
    case CSSValueBolder:
      return FontDescription::BolderWeight(
          state.ParentStyle()->GetFontDescription().Weight());
    case CSSValueLighter:
      return FontDescription::LighterWeight(
          state.ParentStyle()->GetFontDescription().Weight());
    default:
      return identifier_value.ConvertTo<FontWeight>();
  }
}

FontDescription::FontVariantCaps StyleBuilderConverter::ConvertFontVariantCaps(
    StyleResolverState&,
    const CSSValue& value) {
  SECURITY_DCHECK(value.IsIdentifierValue());
  CSSValueID value_id = ToCSSIdentifierValue(value).GetValueID();
  switch (value_id) {
    case CSSValueNormal:
      return FontDescription::kCapsNormal;
    case CSSValueSmallCaps:
      return FontDescription::kSmallCaps;
    case CSSValueAllSmallCaps:
      return FontDescription::kAllSmallCaps;
    case CSSValuePetiteCaps:
      return FontDescription::kPetiteCaps;
    case CSSValueAllPetiteCaps:
      return FontDescription::kAllPetiteCaps;
    case CSSValueUnicase:
      return FontDescription::kUnicase;
    case CSSValueTitlingCaps:
      return FontDescription::kTitlingCaps;
    default:
      return FontDescription::kCapsNormal;
  }
}

FontDescription::VariantLigatures
StyleBuilderConverter::ConvertFontVariantLigatures(StyleResolverState&,
                                                   const CSSValue& value) {
  if (value.IsValueList()) {
    FontDescription::VariantLigatures ligatures;
    const CSSValueList& value_list = ToCSSValueList(value);
    for (size_t i = 0; i < value_list.length(); ++i) {
      const CSSValue& item = value_list.Item(i);
      switch (ToCSSIdentifierValue(item).GetValueID()) {
        case CSSValueNoCommonLigatures:
          ligatures.common = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueCommonLigatures:
          ligatures.common = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueNoDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueNoHistoricalLigatures:
          ligatures.historical = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueHistoricalLigatures:
          ligatures.historical = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueNoContextual:
          ligatures.contextual = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueContextual:
          ligatures.contextual = FontDescription::kEnabledLigaturesState;
          break;
        default:
          NOTREACHED();
          break;
      }
    }
    return ligatures;
  }

  SECURITY_DCHECK(value.IsIdentifierValue());
  if (ToCSSIdentifierValue(value).GetValueID() == CSSValueNone) {
    return FontDescription::VariantLigatures(
        FontDescription::kDisabledLigaturesState);
  }

  DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNormal);
  return FontDescription::VariantLigatures();
}

FontVariantNumeric StyleBuilderConverter::ConvertFontVariantNumeric(
    StyleResolverState&,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNormal);
    return FontVariantNumeric();
  }

  FontVariantNumeric variant_numeric;
  for (const CSSValue* feature : ToCSSValueList(value)) {
    switch (ToCSSIdentifierValue(feature)->GetValueID()) {
      case CSSValueLiningNums:
        variant_numeric.SetNumericFigure(FontVariantNumeric::kLiningNums);
        break;
      case CSSValueOldstyleNums:
        variant_numeric.SetNumericFigure(FontVariantNumeric::kOldstyleNums);
        break;
      case CSSValueProportionalNums:
        variant_numeric.SetNumericSpacing(
            FontVariantNumeric::kProportionalNums);
        break;
      case CSSValueTabularNums:
        variant_numeric.SetNumericSpacing(FontVariantNumeric::kTabularNums);
        break;
      case CSSValueDiagonalFractions:
        variant_numeric.SetNumericFraction(
            FontVariantNumeric::kDiagonalFractions);
        break;
      case CSSValueStackedFractions:
        variant_numeric.SetNumericFraction(
            FontVariantNumeric::kStackedFractions);
        break;
      case CSSValueOrdinal:
        variant_numeric.SetOrdinal(FontVariantNumeric::kOrdinalOn);
        break;
      case CSSValueSlashedZero:
        variant_numeric.SetSlashedZero(FontVariantNumeric::kSlashedZeroOn);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  return variant_numeric;
}

StyleSelfAlignmentData StyleBuilderConverter::ConvertSelfOrDefaultAlignmentData(
    StyleResolverState&,
    const CSSValue& value) {
  StyleSelfAlignmentData alignment_data = ComputedStyle::InitialSelfAlignment();
  if (value.IsValuePair()) {
    const CSSValuePair& pair = ToCSSValuePair(value);
    if (ToCSSIdentifierValue(pair.First()).GetValueID() == CSSValueLegacy) {
      alignment_data.SetPositionType(kLegacyPosition);
      alignment_data.SetPosition(
          ToCSSIdentifierValue(pair.Second()).ConvertTo<ItemPosition>());
    } else if (ToCSSIdentifierValue(pair.First()).GetValueID() ==
               CSSValueFirst) {
      alignment_data.SetPosition(kItemPositionBaseline);
    } else if (ToCSSIdentifierValue(pair.First()).GetValueID() ==
               CSSValueLast) {
      alignment_data.SetPosition(kItemPositionLastBaseline);
    } else {
      alignment_data.SetPosition(
          ToCSSIdentifierValue(pair.First()).ConvertTo<ItemPosition>());
      alignment_data.SetOverflow(
          ToCSSIdentifierValue(pair.Second()).ConvertTo<OverflowAlignment>());
    }
  } else {
    alignment_data.SetPosition(
        ToCSSIdentifierValue(value).ConvertTo<ItemPosition>());
  }
  return alignment_data;
}

StyleContentAlignmentData StyleBuilderConverter::ConvertContentAlignmentData(
    StyleResolverState&,
    const CSSValue& value) {
  StyleContentAlignmentData alignment_data =
      ComputedStyle::InitialContentAlignment();
  if (!RuntimeEnabledFeatures::cssGridLayoutEnabled()) {
    const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
    switch (identifier_value.GetValueID()) {
      case CSSValueStretch:
      case CSSValueSpaceBetween:
      case CSSValueSpaceAround:
        alignment_data.SetDistribution(
            identifier_value.ConvertTo<ContentDistributionType>());
        break;
      case CSSValueFlexStart:
      case CSSValueFlexEnd:
      case CSSValueCenter:
        alignment_data.SetPosition(
            identifier_value.ConvertTo<ContentPosition>());
        break;
      default:
        NOTREACHED();
    }
    return alignment_data;
  }
  const CSSContentDistributionValue& content_value =
      ToCSSContentDistributionValue(value);
  if (content_value.Distribution()->GetValueID() != CSSValueInvalid)
    alignment_data.SetDistribution(
        content_value.Distribution()->ConvertTo<ContentDistributionType>());
  if (content_value.GetPosition()->GetValueID() != CSSValueInvalid)
    alignment_data.SetPosition(
        content_value.GetPosition()->ConvertTo<ContentPosition>());
  if (content_value.Overflow()->GetValueID() != CSSValueInvalid)
    alignment_data.SetOverflow(
        content_value.Overflow()->ConvertTo<OverflowAlignment>());

  return alignment_data;
}

GridAutoFlow StyleBuilderConverter::ConvertGridAutoFlow(StyleResolverState&,
                                                        const CSSValue& value) {
  const CSSValueList& list = ToCSSValueList(value);

  DCHECK_GE(list.length(), 1u);
  const CSSIdentifierValue& first = ToCSSIdentifierValue(list.Item(0));
  const CSSIdentifierValue* second =
      list.length() == 2 ? &ToCSSIdentifierValue(list.Item(1)) : nullptr;

  switch (first.GetValueID()) {
    case CSSValueRow:
      if (second && second->GetValueID() == CSSValueDense)
        return kAutoFlowRowDense;
      return kAutoFlowRow;
    case CSSValueColumn:
      if (second && second->GetValueID() == CSSValueDense)
        return kAutoFlowColumnDense;
      return kAutoFlowColumn;
    case CSSValueDense:
      if (second && second->GetValueID() == CSSValueColumn)
        return kAutoFlowColumnDense;
      return kAutoFlowRowDense;
    default:
      NOTREACHED();
      return ComputedStyle::InitialGridAutoFlow();
  }
}

GridPosition StyleBuilderConverter::ConvertGridPosition(StyleResolverState&,
                                                        const CSSValue& value) {
  // We accept the specification's grammar:
  // 'auto' | [ <integer> || <custom-ident> ] |
  // [ span && [ <integer> || <custom-ident> ] ] | <custom-ident>

  GridPosition position;

  if (value.IsCustomIdentValue()) {
    position.SetNamedGridArea(ToCSSCustomIdentValue(value).Value());
    return position;
  }

  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueAuto);
    return position;
  }

  const CSSValueList& values = ToCSSValueList(value);
  DCHECK(values.length());

  bool is_span_position = false;
  // The specification makes the <integer> optional, in which case it default to
  // '1'.
  int grid_line_number = 1;
  AtomicString grid_line_name;

  auto it = values.begin();
  const CSSValue* current_value = it->Get();
  if (current_value->IsIdentifierValue() &&
      ToCSSIdentifierValue(current_value)->GetValueID() == CSSValueSpan) {
    is_span_position = true;
    ++it;
    current_value = it != values.end() ? it->Get() : nullptr;
  }

  if (current_value && current_value->IsPrimitiveValue() &&
      ToCSSPrimitiveValue(current_value)->IsNumber()) {
    grid_line_number = ToCSSPrimitiveValue(current_value)->GetIntValue();
    ++it;
    current_value = it != values.end() ? it->Get() : nullptr;
  }

  if (current_value && current_value->IsCustomIdentValue()) {
    grid_line_name = ToCSSCustomIdentValue(current_value)->Value();
    ++it;
  }

  DCHECK_EQ(it, values.end());
  if (is_span_position)
    position.SetSpanPosition(grid_line_number, grid_line_name);
  else
    position.SetExplicitPosition(grid_line_number, grid_line_name);

  return position;
}

GridTrackSize StyleBuilderConverter::ConvertGridTrackSize(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsPrimitiveValue() || value.IsIdentifierValue())
    return GridTrackSize(ConvertGridTrackBreadth(state, value));

  auto& function = ToCSSFunctionValue(value);
  if (function.FunctionType() == CSSValueFitContent) {
    SECURITY_DCHECK(function.length() == 1);
    return GridTrackSize(ConvertGridTrackBreadth(state, function.Item(0)),
                         kFitContentTrackSizing);
  }

  SECURITY_DCHECK(function.length() == 2);
  GridLength min_track_breadth(
      ConvertGridTrackBreadth(state, function.Item(0)));
  GridLength max_track_breadth(
      ConvertGridTrackBreadth(state, function.Item(1)));
  return GridTrackSize(min_track_breadth, max_track_breadth);
}

static void ConvertGridLineNamesList(
    const CSSValue& value,
    size_t current_named_grid_line,
    NamedGridLinesMap& named_grid_lines,
    OrderedNamedGridLines& ordered_named_grid_lines) {
  DCHECK(value.IsGridLineNamesValue());

  for (auto& named_grid_line_value : ToCSSValueList(value)) {
    String named_grid_line =
        ToCSSCustomIdentValue(*named_grid_line_value).Value();
    NamedGridLinesMap::AddResult result =
        named_grid_lines.insert(named_grid_line, Vector<size_t>());
    result.stored_value->value.push_back(current_named_grid_line);
    OrderedNamedGridLines::AddResult ordered_insertion_result =
        ordered_named_grid_lines.insert(current_named_grid_line,
                                        Vector<String>());
    ordered_insertion_result.stored_value->value.push_back(named_grid_line);
  }
}

Vector<GridTrackSize> StyleBuilderConverter::ConvertGridTrackSizeList(
    StyleResolverState& state,
    const CSSValue& value) {
  DCHECK(value.IsValueList());
  Vector<GridTrackSize> track_sizes;
  for (auto& curr_value : ToCSSValueList(value)) {
    DCHECK(!curr_value->IsGridLineNamesValue());
    DCHECK(!curr_value->IsGridAutoRepeatValue());
    track_sizes.push_back(ConvertGridTrackSize(state, *curr_value));
  }
  return track_sizes;
}

void StyleBuilderConverter::ConvertGridTrackList(
    const CSSValue& value,
    Vector<GridTrackSize>& track_sizes,
    NamedGridLinesMap& named_grid_lines,
    OrderedNamedGridLines& ordered_named_grid_lines,
    Vector<GridTrackSize>& auto_repeat_track_sizes,
    NamedGridLinesMap& auto_repeat_named_grid_lines,
    OrderedNamedGridLines& auto_repeat_ordered_named_grid_lines,
    size_t& auto_repeat_insertion_point,
    AutoRepeatType& auto_repeat_type,
    StyleResolverState& state) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return;
  }

  size_t current_named_grid_line = 0;
  for (auto curr_value : ToCSSValueList(value)) {
    if (curr_value->IsGridLineNamesValue()) {
      ConvertGridLineNamesList(*curr_value, current_named_grid_line,
                               named_grid_lines, ordered_named_grid_lines);
      continue;
    }

    if (curr_value->IsGridAutoRepeatValue()) {
      DCHECK(auto_repeat_track_sizes.IsEmpty());
      size_t auto_repeat_index = 0;
      CSSValueID auto_repeat_id =
          ToCSSGridAutoRepeatValue(curr_value.Get())->AutoRepeatID();
      DCHECK(auto_repeat_id == CSSValueAutoFill ||
             auto_repeat_id == CSSValueAutoFit);
      auto_repeat_type =
          auto_repeat_id == CSSValueAutoFill ? kAutoFill : kAutoFit;
      for (auto auto_repeat_value : ToCSSValueList(*curr_value)) {
        if (auto_repeat_value->IsGridLineNamesValue()) {
          ConvertGridLineNamesList(*auto_repeat_value, auto_repeat_index,
                                   auto_repeat_named_grid_lines,
                                   auto_repeat_ordered_named_grid_lines);
          continue;
        }
        ++auto_repeat_index;
        auto_repeat_track_sizes.push_back(
            ConvertGridTrackSize(state, *auto_repeat_value));
      }
      auto_repeat_insertion_point = current_named_grid_line++;
      continue;
    }

    ++current_named_grid_line;
    track_sizes.push_back(ConvertGridTrackSize(state, *curr_value));
  }

  // The parser should have rejected any <track-list> without any <track-size>
  // as this is not conformant to the syntax.
  DCHECK(!track_sizes.IsEmpty() || !auto_repeat_track_sizes.IsEmpty());
}

void StyleBuilderConverter::ConvertOrderedNamedGridLinesMapToNamedGridLinesMap(
    const OrderedNamedGridLines& ordered_named_grid_lines,
    NamedGridLinesMap& named_grid_lines) {
  DCHECK_EQ(named_grid_lines.size(), 0u);

  if (ordered_named_grid_lines.size() == 0)
    return;

  for (auto& ordered_named_grid_line : ordered_named_grid_lines) {
    for (auto& line_name : ordered_named_grid_line.value) {
      NamedGridLinesMap::AddResult start_result =
          named_grid_lines.insert(line_name, Vector<size_t>());
      start_result.stored_value->value.push_back(ordered_named_grid_line.key);
    }
  }

  for (auto& named_grid_line : named_grid_lines) {
    Vector<size_t> grid_line_indexes = named_grid_line.value;
    std::sort(grid_line_indexes.begin(), grid_line_indexes.end());
  }
}

void StyleBuilderConverter::CreateImplicitNamedGridLinesFromGridArea(
    const NamedGridAreaMap& named_grid_areas,
    NamedGridLinesMap& named_grid_lines,
    GridTrackSizingDirection direction) {
  for (const auto& named_grid_area_entry : named_grid_areas) {
    GridSpan area_span = direction == kForRows
                             ? named_grid_area_entry.value.rows
                             : named_grid_area_entry.value.columns;
    {
      NamedGridLinesMap::AddResult start_result = named_grid_lines.insert(
          named_grid_area_entry.key + "-start", Vector<size_t>());
      start_result.stored_value->value.push_back(area_span.StartLine());
      std::sort(start_result.stored_value->value.begin(),
                start_result.stored_value->value.end());
    }
    {
      NamedGridLinesMap::AddResult end_result = named_grid_lines.insert(
          named_grid_area_entry.key + "-end", Vector<size_t>());
      end_result.stored_value->value.push_back(area_span.EndLine());
      std::sort(end_result.stored_value->value.begin(),
                end_result.stored_value->value.end());
    }
  }
}

Length StyleBuilderConverter::ConvertLength(const StyleResolverState& state,
                                            const CSSValue& value) {
  return ToCSSPrimitiveValue(value).ConvertToLength(
      state.CssToLengthConversionData());
}

UnzoomedLength StyleBuilderConverter::ConvertUnzoomedLength(
    const StyleResolverState& state,
    const CSSValue& value) {
  return UnzoomedLength(ToCSSPrimitiveValue(value).ConvertToLength(
      state.CssToLengthConversionData().CopyWithAdjustedZoom(1.0f)));
}

Length StyleBuilderConverter::ConvertLengthOrAuto(
    const StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueAuto)
    return Length(kAuto);
  return ToCSSPrimitiveValue(value).ConvertToLength(
      state.CssToLengthConversionData());
}

Length StyleBuilderConverter::ConvertLengthSizing(StyleResolverState& state,
                                                  const CSSValue& value) {
  if (!value.IsIdentifierValue())
    return ConvertLength(state, value);

  const CSSIdentifierValue& identifier_value = ToCSSIdentifierValue(value);
  switch (identifier_value.GetValueID()) {
    case CSSValueMinContent:
    case CSSValueWebkitMinContent:
      return Length(kMinContent);
    case CSSValueMaxContent:
    case CSSValueWebkitMaxContent:
      return Length(kMaxContent);
    case CSSValueWebkitFillAvailable:
      return Length(kFillAvailable);
    case CSSValueWebkitFitContent:
    case CSSValueFitContent:
      return Length(kFitContent);
    case CSSValueAuto:
      return Length(kAuto);
    default:
      NOTREACHED();
      return Length();
  }
}

Length StyleBuilderConverter::ConvertLengthMaxSizing(StyleResolverState& state,
                                                     const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNone)
    return Length(kMaxSizeNone);
  return ConvertLengthSizing(state, value);
}

TabSize StyleBuilderConverter::ConvertLengthOrTabSpaces(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
  if (primitive_value.IsNumber())
    return TabSize(primitive_value.GetIntValue());
  return TabSize(
      primitive_value.ComputeLength<float>(state.CssToLengthConversionData()));
}

static CSSToLengthConversionData LineHeightToLengthConversionData(
    StyleResolverState& state) {
  float multiplier = state.Style()->EffectiveZoom();
  if (LocalFrame* frame = state.GetDocument().GetFrame())
    multiplier *= frame->TextZoomFactor();
  return state.CssToLengthConversionData().CopyWithAdjustedZoom(multiplier);
}

Length StyleBuilderConverter::ConvertLineHeight(StyleResolverState& state,
                                                const CSSValue& value) {
  if (value.IsPrimitiveValue()) {
    const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
    if (primitive_value.IsLength()) {
      return primitive_value.ComputeLength<Length>(
          LineHeightToLengthConversionData(state));
    }
    if (primitive_value.IsPercentage()) {
      return Length(
          (state.Style()->ComputedFontSize() * primitive_value.GetIntValue()) /
              100.0,
          kFixed);
    }
    if (primitive_value.IsNumber()) {
      return Length(clampTo<float>(primitive_value.GetDoubleValue() * 100.0),
                    kPercent);
    }
    if (primitive_value.IsCalculated()) {
      Length zoomed_length = Length(primitive_value.CssCalcValue()->ToCalcValue(
          LineHeightToLengthConversionData(state)));
      return Length(
          ValueForLength(zoomed_length,
                         LayoutUnit(state.Style()->ComputedFontSize())),
          kFixed);
    }
  }

  DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNormal);
  return ComputedStyle::InitialLineHeight();
}

float StyleBuilderConverter::ConvertNumberOrPercentage(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
  DCHECK(primitive_value.IsNumber() || primitive_value.IsPercentage());
  if (primitive_value.IsNumber())
    return primitive_value.GetFloatValue();
  return primitive_value.GetFloatValue() / 100.0f;
}

StyleOffsetRotation StyleBuilderConverter::ConvertOffsetRotate(
    StyleResolverState&,
    const CSSValue& value) {
  return ConvertOffsetRotate(value);
}

StyleOffsetRotation StyleBuilderConverter::ConvertOffsetRotate(
    const CSSValue& value) {
  StyleOffsetRotation result(0, kOffsetRotationFixed);

  const CSSValueList& list = ToCSSValueList(value);
  DCHECK(list.length() == 1 || list.length() == 2);
  for (const auto& item : list) {
    if (item->IsIdentifierValue() &&
        ToCSSIdentifierValue(*item).GetValueID() == CSSValueAuto) {
      result.type = kOffsetRotationAuto;
    } else if (item->IsIdentifierValue() &&
               ToCSSIdentifierValue(*item).GetValueID() == CSSValueReverse) {
      result.type = kOffsetRotationAuto;
      result.angle = clampTo<float>(result.angle + 180);
    } else {
      const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(*item);
      result.angle =
          clampTo<float>(result.angle + primitive_value.ComputeDegrees());
    }
  }

  return result;
}

LengthPoint StyleBuilderConverter::ConvertPosition(StyleResolverState& state,
                                                   const CSSValue& value) {
  const CSSValuePair& pair = ToCSSValuePair(value);
  return LengthPoint(
      ConvertPositionLength<CSSValueLeft, CSSValueRight>(state, pair.First()),
      ConvertPositionLength<CSSValueTop, CSSValueBottom>(state, pair.Second()));
}

LengthPoint StyleBuilderConverter::ConvertPositionOrAuto(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsValuePair())
    return ConvertPosition(state, value);
  DCHECK(ToCSSIdentifierValue(value).GetValueID() == CSSValueAuto);
  return LengthPoint(Length(kAuto), Length(kAuto));
}

static float ConvertPerspectiveLength(
    StyleResolverState& state,
    const CSSPrimitiveValue& primitive_value) {
  return std::max(
      primitive_value.ComputeLength<float>(state.CssToLengthConversionData()),
      0.0f);
}

float StyleBuilderConverter::ConvertPerspective(StyleResolverState& state,
                                                const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNone)
    return ComputedStyle::InitialPerspective();
  return ConvertPerspectiveLength(state, ToCSSPrimitiveValue(value));
}

EPaintOrder StyleBuilderConverter::ConvertPaintOrder(
    StyleResolverState&,
    const CSSValue& css_paint_order) {
  if (css_paint_order.IsValueList()) {
    const CSSValueList& order_type_list = ToCSSValueList(css_paint_order);
    switch (ToCSSIdentifierValue(order_type_list.Item(0)).GetValueID()) {
      case CSSValueFill:
        return order_type_list.length() > 1 ? kPaintOrderFillMarkersStroke
                                            : kPaintOrderFillStrokeMarkers;
      case CSSValueStroke:
        return order_type_list.length() > 1 ? kPaintOrderStrokeMarkersFill
                                            : kPaintOrderStrokeFillMarkers;
      case CSSValueMarkers:
        return order_type_list.length() > 1 ? kPaintOrderMarkersStrokeFill
                                            : kPaintOrderMarkersFillStroke;
      default:
        NOTREACHED();
        return kPaintOrderNormal;
    }
  }

  return kPaintOrderNormal;
}

Length StyleBuilderConverter::ConvertQuirkyLength(StyleResolverState& state,
                                                  const CSSValue& value) {
  Length length = ConvertLengthOrAuto(state, value);
  // This is only for margins which use __qem
  length.SetQuirk(value.IsPrimitiveValue() &&
                  ToCSSPrimitiveValue(value).IsQuirkyEms());
  return length;
}

PassRefPtr<QuotesData> StyleBuilderConverter::ConvertQuotes(
    StyleResolverState&,
    const CSSValue& value) {
  if (value.IsValueList()) {
    const CSSValueList& list = ToCSSValueList(value);
    RefPtr<QuotesData> quotes = QuotesData::Create();
    for (size_t i = 0; i < list.length(); i += 2) {
      String start_quote = ToCSSStringValue(list.Item(i)).Value();
      String end_quote = ToCSSStringValue(list.Item(i + 1)).Value();
      quotes->AddPair(std::make_pair(start_quote, end_quote));
    }
    return quotes.Release();
  }
  DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
  return QuotesData::Create();
}

LengthSize StyleBuilderConverter::ConvertRadius(StyleResolverState& state,
                                                const CSSValue& value) {
  const CSSValuePair& pair = ToCSSValuePair(value);
  Length radius_width = ToCSSPrimitiveValue(pair.First())
                            .ConvertToLength(state.CssToLengthConversionData());
  Length radius_height =
      ToCSSPrimitiveValue(pair.Second())
          .ConvertToLength(state.CssToLengthConversionData());
  return LengthSize(radius_width, radius_height);
}

ShadowData StyleBuilderConverter::ConvertShadow(
    const CSSToLengthConversionData& conversion_data,
    StyleResolverState* state,
    const CSSValue& value) {
  const CSSShadowValue& shadow = ToCSSShadowValue(value);
  float x = shadow.x->ComputeLength<float>(conversion_data);
  float y = shadow.y->ComputeLength<float>(conversion_data);
  float blur =
      shadow.blur ? shadow.blur->ComputeLength<float>(conversion_data) : 0;
  float spread =
      shadow.spread ? shadow.spread->ComputeLength<float>(conversion_data) : 0;
  ShadowStyle shadow_style =
      shadow.style && shadow.style->GetValueID() == CSSValueInset ? kInset
                                                                  : kNormal;
  StyleColor color = StyleColor::CurrentColor();
  if (shadow.color) {
    if (state) {
      color = ConvertStyleColor(*state, *shadow.color);
    } else {
      // For OffScreen canvas, we default to black and only parse non
      // Document dependent CSS colors.
      color = StyleColor(Color::kBlack);
      if (shadow.color->IsColorValue()) {
        color = ToCSSColorValue(*shadow.color).Value();
      } else {
        CSSValueID value_id = ToCSSIdentifierValue(*shadow.color).GetValueID();
        switch (value_id) {
          case CSSValueInvalid:
            NOTREACHED();
          case CSSValueInternalQuirkInherit:
          case CSSValueWebkitLink:
          case CSSValueWebkitActivelink:
          case CSSValueWebkitFocusRingColor:
          case CSSValueCurrentcolor:
            break;
          default:
            color = StyleColor::ColorFromKeyword(value_id);
        }
      }
    }
  }

  return ShadowData(FloatPoint(x, y), blur, spread, shadow_style, color);
}

PassRefPtr<ShadowList> StyleBuilderConverter::ConvertShadowList(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return PassRefPtr<ShadowList>();
  }

  ShadowDataVector shadows;
  for (const auto& item : ToCSSValueList(value)) {
    shadows.push_back(
        ConvertShadow(state.CssToLengthConversionData(), &state, *item));
  }

  return ShadowList::Adopt(shadows);
}

ShapeValue* StyleBuilderConverter::ConvertShapeValue(StyleResolverState& state,
                                                     const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return nullptr;
  }

  if (value.IsImageValue() || value.IsImageGeneratorValue() ||
      value.IsImageSetValue())
    return ShapeValue::CreateImageValue(
        state.GetStyleImage(CSSPropertyShapeOutside, value));

  RefPtr<BasicShape> shape;
  CSSBoxType css_box = kBoxMissing;
  const CSSValueList& value_list = ToCSSValueList(value);
  for (unsigned i = 0; i < value_list.length(); ++i) {
    const CSSValue& value = value_list.Item(i);
    if (value.IsBasicShapeValue()) {
      shape = BasicShapeForValue(state, value);
    } else {
      css_box = ToCSSIdentifierValue(value).ConvertTo<CSSBoxType>();
    }
  }

  if (shape)
    return ShapeValue::CreateShapeValue(shape.Release(), css_box);

  DCHECK_NE(css_box, kBoxMissing);
  return ShapeValue::CreateBoxShapeValue(css_box);
}

float StyleBuilderConverter::ConvertSpacing(StyleResolverState& state,
                                            const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNormal)
    return 0;
  return ToCSSPrimitiveValue(value).ComputeLength<float>(
      state.CssToLengthConversionData());
}

PassRefPtr<SVGDashArray> StyleBuilderConverter::ConvertStrokeDasharray(
    StyleResolverState& state,
    const CSSValue& value) {
  if (!value.IsValueList())
    return SVGComputedStyle::InitialStrokeDashArray();

  const CSSValueList& dashes = ToCSSValueList(value);

  RefPtr<SVGDashArray> array = SVGDashArray::Create();
  size_t length = dashes.length();
  for (size_t i = 0; i < length; ++i) {
    array->push_back(ConvertLength(state, ToCSSPrimitiveValue(dashes.Item(i))));
  }

  return array.Release();
}

StyleColor StyleBuilderConverter::ConvertStyleColor(StyleResolverState& state,
                                                    const CSSValue& value,
                                                    bool for_visited_link) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueCurrentcolor)
    return StyleColor::CurrentColor();
  return state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
      value, Color(), for_visited_link);
}

StyleAutoColor StyleBuilderConverter::ConvertStyleAutoColor(
    StyleResolverState& state,
    const CSSValue& value,
    bool for_visited_link) {
  if (value.IsIdentifierValue()) {
    if (ToCSSIdentifierValue(value).GetValueID() == CSSValueCurrentcolor)
      return StyleAutoColor::CurrentColor();
    if (ToCSSIdentifierValue(value).GetValueID() == CSSValueAuto)
      return StyleAutoColor::AutoColor();
  }
  return state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
      value, Color(), for_visited_link);
}

float StyleBuilderConverter::ConvertTextStrokeWidth(StyleResolverState& state,
                                                    const CSSValue& value) {
  if (value.IsIdentifierValue() && ToCSSIdentifierValue(value).GetValueID()) {
    float multiplier = ConvertLineWidth<float>(state, value);
    return CSSPrimitiveValue::Create(multiplier / 48,
                                     CSSPrimitiveValue::UnitType::kEms)
        ->ComputeLength<float>(state.CssToLengthConversionData());
  }
  return ToCSSPrimitiveValue(value).ComputeLength<float>(
      state.CssToLengthConversionData());
}

TextSizeAdjust StyleBuilderConverter::ConvertTextSizeAdjust(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueNone)
    return TextSizeAdjust::AdjustNone();
  if (value.IsIdentifierValue() &&
      ToCSSIdentifierValue(value).GetValueID() == CSSValueAuto)
    return TextSizeAdjust::AdjustAuto();
  const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
  DCHECK(primitive_value.IsPercentage());
  return TextSizeAdjust(primitive_value.GetFloatValue() / 100.0f);
}

TransformOperations StyleBuilderConverter::ConvertTransformOperations(
    StyleResolverState& state,
    const CSSValue& value) {
  return TransformBuilder::CreateTransformOperations(
      value, state.CssToLengthConversionData());
}

TransformOrigin StyleBuilderConverter::ConvertTransformOrigin(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSValueList& list = ToCSSValueList(value);
  DCHECK_EQ(list.length(), 3U);
  DCHECK(list.Item(0).IsPrimitiveValue() || list.Item(0).IsIdentifierValue());
  DCHECK(list.Item(1).IsPrimitiveValue() || list.Item(1).IsIdentifierValue());
  DCHECK(list.Item(2).IsPrimitiveValue());

  return TransformOrigin(
      ConvertPositionLength<CSSValueLeft, CSSValueRight>(state, list.Item(0)),
      ConvertPositionLength<CSSValueTop, CSSValueBottom>(state, list.Item(1)),
      StyleBuilderConverter::ConvertComputedLength<float>(state, list.Item(2)));
}

ScrollSnapPoints StyleBuilderConverter::ConvertSnapPoints(
    StyleResolverState& state,
    const CSSValue& value) {
  // Handles: none | repeat(<length>)
  ScrollSnapPoints points;
  points.has_repeat = false;

  if (!value.IsFunctionValue())
    return points;

  const CSSFunctionValue& repeat_function = ToCSSFunctionValue(value);
  SECURITY_DCHECK(repeat_function.length() == 1);
  points.repeat_offset =
      ConvertLength(state, ToCSSPrimitiveValue(repeat_function.Item(0)));
  points.has_repeat = true;

  return points;
}

Vector<LengthPoint> StyleBuilderConverter::ConvertSnapCoordinates(
    StyleResolverState& state,
    const CSSValue& value) {
  // Handles: none | <position>#
  Vector<LengthPoint> coordinates;

  if (!value.IsValueList())
    return coordinates;

  const CSSValueList& value_list = ToCSSValueList(value);
  coordinates.ReserveInitialCapacity(value_list.length());
  for (auto& snap_coordinate : value_list) {
    coordinates.UncheckedAppend(ConvertPosition(state, *snap_coordinate));
  }

  return coordinates;
}

PassRefPtr<TranslateTransformOperation> StyleBuilderConverter::ConvertTranslate(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return nullptr;
  }
  const CSSValueList& list = ToCSSValueList(value);
  DCHECK_LE(list.length(), 3u);
  Length tx = ConvertLength(state, list.Item(0));
  Length ty(0, kFixed);
  double tz = 0;
  if (list.length() >= 2)
    ty = ConvertLength(state, list.Item(1));
  if (list.length() == 3)
    tz = ToCSSPrimitiveValue(list.Item(2))
             .ComputeLength<double>(state.CssToLengthConversionData());

  return TranslateTransformOperation::Create(tx, ty, tz,
                                             TransformOperation::kTranslate3D);
}

Rotation StyleBuilderConverter::ConvertRotation(const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return Rotation(FloatPoint3D(0, 0, 1), 0);
  }

  const CSSValueList& list = ToCSSValueList(value);
  DCHECK(list.length() == 1 || list.length() == 4);
  double x = 0;
  double y = 0;
  double z = 1;
  if (list.length() == 4) {
    x = ToCSSPrimitiveValue(list.Item(0)).GetDoubleValue();
    y = ToCSSPrimitiveValue(list.Item(1)).GetDoubleValue();
    z = ToCSSPrimitiveValue(list.Item(2)).GetDoubleValue();
  }
  double angle =
      ToCSSPrimitiveValue(list.Item(list.length() - 1)).ComputeDegrees();
  return Rotation(FloatPoint3D(x, y, z), angle);
}

PassRefPtr<RotateTransformOperation> StyleBuilderConverter::ConvertRotate(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return nullptr;
  }

  return RotateTransformOperation::Create(ConvertRotation(value),
                                          TransformOperation::kRotate3D);
}

PassRefPtr<ScaleTransformOperation> StyleBuilderConverter::ConvertScale(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsIdentifierValue()) {
    DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
    return nullptr;
  }

  const CSSValueList& list = ToCSSValueList(value);
  DCHECK_LE(list.length(), 3u);
  double sx = ToCSSPrimitiveValue(list.Item(0)).GetDoubleValue();
  double sy = 1;
  double sz = 1;
  if (list.length() >= 2)
    sy = ToCSSPrimitiveValue(list.Item(1)).GetDoubleValue();
  if (list.length() == 3)
    sz = ToCSSPrimitiveValue(list.Item(2)).GetDoubleValue();

  return ScaleTransformOperation::Create(sx, sy, sz,
                                         TransformOperation::kScale3D);
}

RespectImageOrientationEnum StyleBuilderConverter::ConvertImageOrientation(
    StyleResolverState& state,
    const CSSValue& value) {
  return value.IsIdentifierValue() &&
                 ToCSSIdentifierValue(value).GetValueID() == CSSValueFromImage
             ? kRespectImageOrientation
             : kDoNotRespectImageOrientation;
}

PassRefPtr<StylePath> StyleBuilderConverter::ConvertPathOrNone(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsPathValue())
    return ToCSSPathValue(value).GetStylePath();
  DCHECK_EQ(ToCSSIdentifierValue(value).GetValueID(), CSSValueNone);
  return nullptr;
}

static const CSSValue& ComputeRegisteredPropertyValue(
    const CSSToLengthConversionData& css_to_length_conversion_data,
    const CSSValue& value) {
  // TODO(timloh): Images and transform-function values can also contain
  // lengths.
  if (value.IsValueList()) {
    CSSValueList* new_list = CSSValueList::CreateSpaceSeparated();
    for (const CSSValue* inner_value : ToCSSValueList(value)) {
      new_list->Append(ComputeRegisteredPropertyValue(
          css_to_length_conversion_data, *inner_value));
    }
    return *new_list;
  }

  if (value.IsPrimitiveValue()) {
    const CSSPrimitiveValue& primitive_value = ToCSSPrimitiveValue(value);
    if ((primitive_value.IsCalculated() &&
         (primitive_value.IsCalculatedPercentageWithLength() ||
          primitive_value.IsLength() || primitive_value.IsPercentage())) ||
        CSSPrimitiveValue::IsRelativeUnit(
            primitive_value.TypeWithCalcResolved())) {
      // Instead of the actual zoom, use 1 to avoid potential rounding errors
      Length length = primitive_value.ConvertToLength(
          css_to_length_conversion_data.CopyWithAdjustedZoom(1));
      return *CSSPrimitiveValue::Create(length, 1);
    }
  }
  return value;
}

const CSSValue& StyleBuilderConverter::ConvertRegisteredPropertyInitialValue(
    const CSSValue& value) {
  return ComputeRegisteredPropertyValue(CSSToLengthConversionData(), value);
}

const CSSValue& StyleBuilderConverter::ConvertRegisteredPropertyValue(
    const StyleResolverState& state,
    const CSSValue& value) {
  return ComputeRegisteredPropertyValue(state.CssToLengthConversionData(),
                                        value);
}

}  // namespace blink
