// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/FrameView.h"
#include "core/layout/LayoutTestHelper.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintLayer.h"
#include "platform/testing/RuntimeEnabledFeaturesTestHelpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

class PaintInvalidationTest : public ::testing::WithParamInterface<bool>,
                              private ScopedRootLayerScrollingForTest,
                              public RenderingTest {
 public:
  PaintInvalidationTest()
      : ScopedRootLayerScrollingForTest(GetParam()),
        RenderingTest(SingleChildLocalFrameClient::Create()) {}
};

INSTANTIATE_TEST_CASE_P(All, PaintInvalidationTest, ::testing::Bool());

// Changing style in a way that changes overflow without layout should cause
// the layout view to possibly need a paint invalidation since we may have
// revealed additional background that can be scrolled into view.
TEST_P(PaintInvalidationTest, RecalcOverflowInvalidatesBackground) {
  GetDocument().GetPage()->GetSettings().SetViewportEnabled(true);
  SetBodyInnerHTML(
      "<!DOCTYPE html>"
      "<style type='text/css'>"
      "  body, html {"
      "    width: 100%;"
      "    height: 100%;"
      "    margin: 0px;"
      "  }"
      "  #container {"
      "    width: 100%;"
      "    height: 100%;"
      "  }"
      "</style>"
      "<div id='container'></div>");

  GetDocument().View()->UpdateAllLifecyclePhases();

  ScrollableArea* scrollable_area = GetDocument().View();
  ASSERT_EQ(scrollable_area->MaximumScrollOffset().Height(), 0);
  EXPECT_FALSE(GetDocument().GetLayoutView()->MayNeedPaintInvalidation());

  Element* container = GetDocument().GetElementById("container");
  container->setAttribute(HTMLNames::styleAttr,
                          "transform: translateY(1000px);");
  GetDocument().UpdateStyleAndLayoutTree();

  EXPECT_EQ(scrollable_area->MaximumScrollOffset().Height(), 1000);
  EXPECT_TRUE(GetDocument().GetLayoutView()->MayNeedPaintInvalidation());
}

TEST_P(PaintInvalidationTest, UpdateVisualRectOnFrameBorderWidthChange) {
  // TODO(wangxianzhu): enable for SPv2.
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;

  SetBodyInnerHTML(
      "<style>"
      "  body { margin: 10px }"
      "  iframe { width: 100px; height: 100px; border: none; }"
      "</style>"
      "<iframe id='iframe'></iframe>");

  Element* iframe = GetDocument().GetElementById("iframe");
  LayoutView* child_layout_view = ChildDocument().GetLayoutView();
  EXPECT_EQ(GetDocument().GetLayoutView(),
            &child_layout_view->ContainerForPaintInvalidation());
  EXPECT_EQ(LayoutRect(10, 10, 100, 100), child_layout_view->VisualRect());

  iframe->setAttribute(HTMLNames::styleAttr, "border: 20px solid blue");
  GetDocument().View()->UpdateAllLifecyclePhases();
  EXPECT_EQ(GetDocument().GetLayoutView(),
            &child_layout_view->ContainerForPaintInvalidation());
  EXPECT_EQ(LayoutRect(30, 30, 100, 100), child_layout_view->VisualRect());
};

// This is a simplified test case for crbug.com/704182. It ensures no repaint
// on transform change causing no visual change.
TEST_P(PaintInvalidationTest, InvisibleTransformUnderFixedOnScroll) {
  EnableCompositing();
  SetBodyInnerHTML(
      "<style>"
      "  #fixed {"
      "    position: fixed;"
      "    top: 0;"
      "    left: 0;"
      "    width: 100px;"
      "    height: 100px;"
      "    background-color: blue;"
      "  }"
      "  #transform {"
      "    width: 100px;"
      "    height: 100px;"
      "    background-color: yellow;"
      "    will-change: transform;"
      "    transform: translate(10px, 20px);"
      "  }"
      "</style>"
      "<div style='height: 2000px'></div>"
      "<div id='fixed' style='visibility: hidden'>"
      "  <div id='transform'></div>"
      "</div>");

  auto& fixed = *GetDocument().GetElementById("fixed");
  const auto& fixed_object = ToLayoutBox(*fixed.GetLayoutObject());
  const auto& fixed_layer = *fixed_object.Layer();
  auto& transform = *GetDocument().GetElementById("transform");
  EXPECT_TRUE(fixed_layer.SubtreeIsInvisible());
  EXPECT_EQ(LayoutRect(0, 0, 110, 120), fixed_object.LayoutOverflowRect());

  GetDocument().domWindow()->scrollTo(0, 100);
  transform.setAttribute(HTMLNames::styleAttr,
                         "transform: translate(20px, 30px)");
  GetDocument().View()->UpdateLifecycleToCompositingCleanPlusScrolling();

  EXPECT_TRUE(fixed_layer.SubtreeIsInvisible());
  // We skip invisible layers when setting non-composited fixed-position
  // needing paint invalidation when the frame is scrolled.
  EXPECT_FALSE(fixed_object.ShouldDoFullPaintInvalidation());
  // This was set when fixedObject is marked needsOverflowRecaldAfterStyleChange
  // when child changed transform.
  EXPECT_TRUE(fixed_object.MayNeedPaintInvalidation());
  EXPECT_EQ(LayoutRect(0, 0, 120, 130), fixed_object.LayoutOverflowRect());

  // We should not repaint anything because all contents are invisible.
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_FALSE(fixed_layer.NeedsRepaint());
  GetDocument().View()->UpdateAllLifecyclePhases();

  // The following ensures normal paint invalidation still works.
  transform.setAttribute(
      HTMLNames::styleAttr,
      "visibility: visible; transform: translate(20px, 30px)");
  GetDocument().View()->UpdateLifecycleToCompositingCleanPlusScrolling();
  EXPECT_FALSE(fixed_layer.SubtreeIsInvisible());
  GetDocument().View()->UpdateAllLifecyclePhases();
  fixed.setAttribute(HTMLNames::styleAttr, "top: 50px");
  GetDocument().View()->UpdateLifecycleToCompositingCleanPlusScrolling();
  EXPECT_TRUE(fixed_object.MayNeedPaintInvalidation());
  EXPECT_FALSE(fixed_layer.SubtreeIsInvisible());
  GetDocument().View()->UpdateAllLifecyclePhasesExceptPaint();
  EXPECT_TRUE(fixed_layer.NeedsRepaint());
  GetDocument().View()->UpdateAllLifecyclePhases();
}

}  // namespace

}  // namespace blink
