// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "flutter/display_list/display_list_color_filter.h"
#include "flutter/display_list/display_list_image_filter.h"
#include "flutter/flow/layers/layer.h"
#include "flutter/flow/layers/layer_state_stack.h"
#include "flutter/testing/display_list_testing.h"
#include "flutter/testing/mock_canvas.h"

namespace flutter {
namespace testing {

TEST(LayerStateStack, Defaults) {
  LayerStateStack state_stack;

  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), nullptr);
  ASSERT_EQ(state_stack.checkerboard_func(), nullptr);
  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_bounds(), SkRect());
  ASSERT_EQ(state_stack.device_cull_rect(), kGiantRect);
  ASSERT_EQ(state_stack.local_cull_rect(), kGiantRect);
  ASSERT_EQ(state_stack.transform_3x3(), SkMatrix::I());
  ASSERT_EQ(state_stack.transform_4x4(), SkM44());

  SkPaint sk_paint;
  state_stack.fill(sk_paint);
  ASSERT_EQ(sk_paint, SkPaint());

  DlPaint dl_paint;
  state_stack.fill(dl_paint);
  ASSERT_EQ(dl_paint, DlPaint());
}

TEST(LayerStateStack, SingularDelegate) {
  LayerStateStack state_stack;
  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), nullptr);

  DisplayListBuilder builder;
  MockCanvas canvas;

  // no delegate -> builder delegate
  state_stack.set_delegate(&builder);
  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), &builder);

  // builder delegate -> canvas delegate
  state_stack.set_delegate(&canvas);
  ASSERT_EQ(state_stack.canvas_delegate(), &canvas);
  ASSERT_EQ(state_stack.builder_delegate(), nullptr);

  // canvas delegate -> builder delegate
  state_stack.set_delegate(&builder);
  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), &builder);

  // builder delegate -> no delegate
  state_stack.clear_delegate();
  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), nullptr);

  // canvas delegate -> no delegate
  state_stack.set_delegate(&canvas);
  state_stack.clear_delegate();
  ASSERT_EQ(state_stack.canvas_delegate(), nullptr);
  ASSERT_EQ(state_stack.builder_delegate(), nullptr);
}

TEST(LayerStateStack, Opacity) {
  SkRect rect = {10, 10, 20, 20};

  LayerStateStack state_stack;
  {
    auto mutator = state_stack.save();
    mutator.applyOpacity(rect, 0.5f);

    ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
    ASSERT_EQ(state_stack.outstanding_bounds(), rect);

    // Check nested opacities multiply with each other
    {
      auto mutator2 = state_stack.save();
      mutator.applyOpacity(rect, 0.5f);

      ASSERT_EQ(state_stack.outstanding_opacity(), 0.25f);
      ASSERT_EQ(state_stack.outstanding_bounds(), rect);

      // Verify output with applyState that does not accept opacity
      {
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(rect, 0);
          ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
          ASSERT_EQ(state_stack.outstanding_bounds(), SkRect());

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        DlPaint save_paint =
            DlPaint().setOpacity(state_stack.outstanding_opacity());
        expected.saveLayer(&rect, &save_paint);
        expected.drawRect(rect, DlPaint());
        expected.restore();
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }

      // Verify output with applyState that accepts opacity
      {
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(
              rect, LayerStateStack::kCallerCanApplyOpacity);
          ASSERT_EQ(state_stack.outstanding_opacity(), 0.25f);
          ASSERT_EQ(state_stack.outstanding_bounds(), rect);

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        expected.drawRect(rect, DlPaint().setOpacity(0.25f));
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }
    }

    ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
    ASSERT_EQ(state_stack.outstanding_bounds(), rect);
  }

  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
  ASSERT_EQ(state_stack.outstanding_bounds(), SkRect());
}

TEST(LayerStateStack, ColorFilter) {
  SkRect rect = {10, 10, 20, 20};
  std::shared_ptr<DlBlendColorFilter> outer_filter =
      std::make_shared<DlBlendColorFilter>(DlColor::kYellow(),
                                           DlBlendMode::kColorBurn);
  std::shared_ptr<DlBlendColorFilter> inner_filter =
      std::make_shared<DlBlendColorFilter>(DlColor::kRed(),
                                           DlBlendMode::kColorBurn);

  LayerStateStack state_stack;
  {
    auto mutator = state_stack.save();
    mutator.applyColorFilter(rect, outer_filter);

    ASSERT_EQ(state_stack.outstanding_color_filter(), outer_filter);

    // Check nested color filters result in nested saveLayers
    {
      auto mutator2 = state_stack.save();
      mutator.applyColorFilter(rect, inner_filter);

      ASSERT_EQ(state_stack.outstanding_color_filter(), inner_filter);

      // Verify output with applyState that does not accept color filters
      {
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(rect, 0);
          ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        DlPaint outer_save_paint = DlPaint().setColorFilter(outer_filter);
        DlPaint inner_save_paint = DlPaint().setColorFilter(inner_filter);
        expected.saveLayer(&rect, &outer_save_paint);
        expected.saveLayer(&rect, &inner_save_paint);
        expected.drawRect(rect, DlPaint());
        expected.restore();
        expected.restore();
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }

      // Verify output with applyState that accepts color filters
      {
        SkRect rect = {10, 10, 20, 20};
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(
              rect, LayerStateStack::kCallerCanApplyColorFilter);
          ASSERT_EQ(state_stack.outstanding_color_filter(), inner_filter);

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        DlPaint save_paint = DlPaint().setColorFilter(outer_filter);
        DlPaint draw_paint = DlPaint().setColorFilter(inner_filter);
        expected.saveLayer(&rect, &save_paint);
        expected.drawRect(rect, draw_paint);
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }
    }

    ASSERT_EQ(state_stack.outstanding_color_filter(), outer_filter);
  }

  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
}

TEST(LayerStateStack, ImageFilter) {
  SkRect rect = {10, 10, 20, 20};
  std::shared_ptr<DlBlurImageFilter> outer_filter =
      std::make_shared<DlBlurImageFilter>(2.0f, 2.0f, DlTileMode::kClamp);
  std::shared_ptr<DlBlurImageFilter> inner_filter =
      std::make_shared<DlBlurImageFilter>(3.0f, 3.0f, DlTileMode::kClamp);
  SkRect inner_src_rect = rect;
  SkRect outer_src_rect;
  ASSERT_EQ(inner_filter->map_local_bounds(rect, outer_src_rect),
            &outer_src_rect);

  LayerStateStack state_stack;
  {
    auto mutator = state_stack.save();
    mutator.applyImageFilter(outer_src_rect, outer_filter);

    ASSERT_EQ(state_stack.outstanding_image_filter(), outer_filter);

    // Check nested color filters result in nested saveLayers
    {
      auto mutator2 = state_stack.save();
      mutator.applyImageFilter(rect, inner_filter);

      ASSERT_EQ(state_stack.outstanding_image_filter(), inner_filter);

      // Verify output with applyState that does not accept color filters
      {
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(rect, 0);
          ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        DlPaint outer_save_paint = DlPaint().setImageFilter(outer_filter);
        DlPaint inner_save_paint = DlPaint().setImageFilter(inner_filter);
        expected.saveLayer(&outer_src_rect, &outer_save_paint);
        expected.saveLayer(&inner_src_rect, &inner_save_paint);
        expected.drawRect(rect, DlPaint());
        expected.restore();
        expected.restore();
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }

      // Verify output with applyState that accepts color filters
      {
        SkRect rect = {10, 10, 20, 20};
        DisplayListBuilder builder;
        state_stack.set_delegate(&builder);
        {
          auto restore = state_stack.applyState(
              rect, LayerStateStack::kCallerCanApplyImageFilter);
          ASSERT_EQ(state_stack.outstanding_image_filter(), inner_filter);

          DlPaint paint;
          state_stack.fill(paint);
          builder.drawRect(rect, paint);
        }
        state_stack.clear_delegate();

        DisplayListBuilder expected;
        DlPaint save_paint = DlPaint().setImageFilter(outer_filter);
        DlPaint draw_paint = DlPaint().setImageFilter(inner_filter);
        expected.saveLayer(&outer_src_rect, &save_paint);
        expected.drawRect(rect, draw_paint);
        ASSERT_TRUE(DisplayListsEQ_Verbose(builder.Build(), expected.Build()));
      }
    }

    ASSERT_EQ(state_stack.outstanding_image_filter(), outer_filter);
  }

  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
}

TEST(LayerStateStack, OpacityAndColorFilterInteraction) {
  SkRect rect = {10, 10, 20, 20};
  std::shared_ptr<DlBlendColorFilter> color_filter =
      std::make_shared<DlBlendColorFilter>(DlColor::kYellow(),
                                           DlBlendMode::kColorBurn);

  LayerStateStack state_stack;
  DisplayListBuilder builder;
  state_stack.set_delegate(&builder);
  ASSERT_EQ(builder.getSaveCount(), 1);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyOpacity(rect, 0.5f);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyColorFilter(rect, color_filter);

      // The opacity will have been resolved by a saveLayer
      ASSERT_EQ(builder.getSaveCount(), 4);
      ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
      ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
    ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyColorFilter(rect, color_filter);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyOpacity(rect, 0.5f);

      // color filter applied to opacity can be applied together
      ASSERT_EQ(builder.getSaveCount(), 3);
      ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
      ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
    ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
}

TEST(LayerStateStack, OpacityAndImageFilterInteraction) {
  SkRect rect = {10, 10, 20, 20};
  std::shared_ptr<DlBlurImageFilter> image_filter =
      std::make_shared<DlBlurImageFilter>(2.0f, 2.0f, DlTileMode::kClamp);

  LayerStateStack state_stack;
  DisplayListBuilder builder;
  state_stack.set_delegate(&builder);
  ASSERT_EQ(builder.getSaveCount(), 1);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyOpacity(rect, 0.5f);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyImageFilter(rect, image_filter);

      // opacity applied to image filter can be applied together
      ASSERT_EQ(builder.getSaveCount(), 3);
      ASSERT_EQ(state_stack.outstanding_image_filter(), image_filter);
      ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
    ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyImageFilter(rect, image_filter);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyOpacity(rect, 0.5f);

      // The image filter will have been resolved by a saveLayer
      ASSERT_EQ(builder.getSaveCount(), 4);
      ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
      ASSERT_EQ(state_stack.outstanding_opacity(), 0.5f);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_image_filter(), image_filter);
    ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_opacity(), SK_Scalar1);
}

TEST(LayerStateStack, ColorFilterAndImageFilterInteraction) {
  SkRect rect = {10, 10, 20, 20};
  std::shared_ptr<DlBlendColorFilter> color_filter =
      std::make_shared<DlBlendColorFilter>(DlColor::kYellow(),
                                           DlBlendMode::kColorBurn);
  std::shared_ptr<DlBlurImageFilter> image_filter =
      std::make_shared<DlBlurImageFilter>(2.0f, 2.0f, DlTileMode::kClamp);

  LayerStateStack state_stack;
  DisplayListBuilder builder;
  state_stack.set_delegate(&builder);
  ASSERT_EQ(builder.getSaveCount(), 1);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyColorFilter(rect, color_filter);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyImageFilter(rect, image_filter);

      // color filter applied to image filter can be applied together
      ASSERT_EQ(builder.getSaveCount(), 3);
      ASSERT_EQ(state_stack.outstanding_image_filter(), image_filter);
      ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
    ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);

  {
    auto mutator1 = state_stack.save();
    ASSERT_EQ(builder.getSaveCount(), 2);
    mutator1.applyImageFilter(rect, image_filter);
    ASSERT_EQ(builder.getSaveCount(), 2);

    {
      auto mutator2 = state_stack.save();
      ASSERT_EQ(builder.getSaveCount(), 3);
      mutator2.applyColorFilter(rect, color_filter);

      // The image filter will have been resolved by a saveLayer
      ASSERT_EQ(builder.getSaveCount(), 4);
      ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
      ASSERT_EQ(state_stack.outstanding_color_filter(), color_filter);
    }
    ASSERT_EQ(builder.getSaveCount(), 2);
    ASSERT_EQ(state_stack.outstanding_image_filter(), image_filter);
    ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
  }
  ASSERT_EQ(builder.getSaveCount(), 1);
  ASSERT_EQ(state_stack.outstanding_image_filter(), nullptr);
  ASSERT_EQ(state_stack.outstanding_color_filter(), nullptr);
}

}  // namespace testing
}  // namespace flutter
