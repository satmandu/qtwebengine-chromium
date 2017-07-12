// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSInterpolationType_h
#define CSSInterpolationType_h

#include "core/animation/InterpolationEnvironment.h"
#include "core/animation/InterpolationType.h"

namespace blink {

class CSSCustomPropertyDeclaration;
class PropertyRegistration;

class CSSInterpolationType : public InterpolationType {
 public:
  void SetCustomPropertyRegistration(const PropertyRegistration&);

 protected:
  CSSInterpolationType(PropertyHandle);

  CSSPropertyID CssProperty() const { return GetProperty().CssProperty(); }

  InterpolationValue MaybeConvertSingle(const PropertySpecificKeyframe&,
                                        const InterpolationEnvironment&,
                                        const InterpolationValue& underlying,
                                        ConversionCheckers&) const final;
  virtual InterpolationValue MaybeConvertNeutral(
      const InterpolationValue& underlying,
      ConversionCheckers&) const = 0;
  virtual InterpolationValue MaybeConvertInitial(const StyleResolverState&,
                                                 ConversionCheckers&) const = 0;
  virtual InterpolationValue MaybeConvertInherit(const StyleResolverState&,
                                                 ConversionCheckers&) const = 0;
  virtual InterpolationValue MaybeConvertValue(const CSSValue&,
                                               const StyleResolverState*,
                                               ConversionCheckers&) const = 0;
  virtual void AdditiveKeyframeHook(InterpolationValue&) const {}

  InterpolationValue MaybeConvertUnderlyingValue(
      const InterpolationEnvironment&) const final;
  virtual InterpolationValue MaybeConvertStandardPropertyUnderlyingValue(
      const ComputedStyle&) const = 0;

  void Apply(const InterpolableValue&,
             const NonInterpolableValue*,
             InterpolationEnvironment&) const final;
  virtual void ApplyStandardPropertyValue(const InterpolableValue&,
                                          const NonInterpolableValue*,
                                          StyleResolverState&) const = 0;

 private:
  InterpolationValue MaybeConvertSingleInternal(
      const PropertySpecificKeyframe&,
      const InterpolationEnvironment&,
      const InterpolationValue& underlying,
      ConversionCheckers&) const;

  InterpolationValue MaybeConvertCustomPropertyDeclaration(
      const CSSCustomPropertyDeclaration&,
      const StyleResolverState&,
      ConversionCheckers&) const;
  InterpolationValue MaybeConvertCustomPropertyDeclarationInternal(
      const CSSCustomPropertyDeclaration&,
      const StyleResolverState&,
      ConversionCheckers&) const;

  virtual const CSSValue* CreateCSSValue(const InterpolableValue&,
                                         const NonInterpolableValue*,
                                         const StyleResolverState&) const {
    // TODO(alancutter): Implement this for all subclasses and make this an
    // abstract declaration so the return type can be changed to
    // const CSSValue&.
    NOTREACHED();
    return nullptr;
  }

  void ApplyCustomPropertyValue(const InterpolableValue&,
                                const NonInterpolableValue*,
                                StyleResolverState&) const;

  WeakPersistent<const PropertyRegistration> registration_;
};

}  // namespace blink

#endif  // CSSInterpolationType_h
