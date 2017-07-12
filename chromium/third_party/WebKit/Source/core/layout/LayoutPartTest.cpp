// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/LayoutPart.h"

#include "core/html/HTMLElement.h"
#include "core/layout/ImageQualityController.h"
#include "core/layout/LayoutTestHelper.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class LayoutPartTest : public RenderingTest {};

class OverriddenLayoutPart : public LayoutPart {
 public:
  explicit OverriddenLayoutPart(Element* element) : LayoutPart(element) {}

  const char* GetName() const override { return "OverriddenLayoutPart"; }
};

TEST_F(LayoutPartTest, DestroyUpdatesImageQualityController) {
  Element* element = HTMLElement::Create(HTMLNames::divTag, GetDocument());
  LayoutObject* part = new OverriddenLayoutPart(element);
  // The third and forth arguments are not important in this test.
  ImageQualityController::GetImageQualityController()->Set(
      *part, 0, this, LayoutSize(1, 1), false);
  EXPECT_TRUE(ImageQualityController::Has(*part));
  part->Destroy();
  EXPECT_FALSE(ImageQualityController::Has(*part));
}

}  // namespace blink
