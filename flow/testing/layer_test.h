// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLOW_TESTING_LAYER_TEST_H_
#define FLOW_TESTING_LAYER_TEST_H_

#include "flutter/flow/layer_snapshot_store.h"
#include "flutter/flow/layers/layer.h"

#include <optional>
#include <utility>
#include <vector>

#include "flutter/display_list/display_list_builder_multiplexer.h"
#include "flutter/flow/testing/mock_raster_cache.h"
#include "flutter/fml/macros.h"
#include "flutter/testing/canvas_test.h"
#include "flutter/testing/display_list_testing.h"
#include "flutter/testing/mock_canvas.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/utils/SkNWayCanvas.h"

namespace flutter {
namespace testing {

// This fixture allows generating tests which can |Paint()| and |Preroll()|
// |Layer|'s.
// |LayerTest| is a default implementation based on |::testing::Test|.
//
// By default the preroll and paint contexts will not use a raster cache.
// If a test needs to verify the proper operation of a layer in the presence
// of a raster cache then a number of options can be enabled by using the
// methods |LayerTestBase::use_null_raster_cache()|,
// |LayerTestBase::use_mock_raster_cache()| or
// |LayerTestBase::use_skia_raster_cache()|
//
// |BaseT| should be the base test type, such as |::testing::Test| below.
template <typename BaseT>
class LayerTestBase : public CanvasTestBase<BaseT> {
  using TestT = CanvasTestBase<BaseT>;

  const SkRect kDlBounds = SkRect::MakeWH(500, 500);

 public:
  LayerTestBase()
      : texture_registry_(std::make_shared<TextureRegistry>()),
        preroll_context_{
            // clang-format off
            .raster_cache                  = nullptr,
            .gr_context                    = nullptr,
            .view_embedder                 = nullptr,
            .state_stack                   = preroll_state_stack_,
            .dst_color_space               = TestT::mock_color_space(),
            .surface_needs_readback        = false,
            .raster_time                   = raster_time_,
            .ui_time                       = ui_time_,
            .texture_registry              = texture_registry_,
            .frame_device_pixel_ratio      = 1.0f,
            .has_platform_view             = false,
            .raster_cached_entries         = &cacheable_items_,
            // clang-format on
        },
        paint_context_{
            // clang-format off
            .state_stack                   = paint_state_stack_,
            .canvas                        = &TestT::mock_canvas(),
            .gr_context                    = nullptr,
            .view_embedder                 = nullptr,
            .raster_time                   = raster_time_,
            .ui_time                       = ui_time_,
            .texture_registry              = texture_registry_,
            .raster_cache                  = nullptr,
            .frame_device_pixel_ratio      = 1.0f,
            // clang-format on
        },
        display_list_recorder_(kDlBounds),
        display_list_paint_context_{
            // clang-format off
            .state_stack                   = display_list_state_stack_,
            .canvas                        = &display_list_recorder_,
            .builder                       = display_list_recorder_.builder().get(),
            .gr_context                    = nullptr,
            .view_embedder                 = nullptr,
            .raster_time                   = raster_time_,
            .ui_time                       = ui_time_,
            .texture_registry              = texture_registry_,
            .raster_cache                  = nullptr,
            .frame_device_pixel_ratio      = 1.0f,
            // clang-format on
        },
        checkerboard_context_{
            // clang-format off
            .state_stack                   = checkerboard_state_stack_,
            .canvas                        = &TestT::mock_canvas(),
            .gr_context                    = nullptr,
            .view_embedder                 = nullptr,
            .raster_time                   = raster_time_,
            .ui_time                       = ui_time_,
            .texture_registry              = texture_registry_,
            .raster_cache                  = nullptr,
            .frame_device_pixel_ratio      = 1.0f,
            // clang-format on
        } {
    use_null_raster_cache();
    preroll_state_stack_.set_delegate(&mutators_stack_);
    paint_state_stack_.set_delegate(&TestT::mock_canvas());
    display_list_state_stack_.set_delegate(display_list_recorder_);
    checkerboard_state_stack_.set_delegate(&TestT::mock_canvas());
    checkerboard_state_stack_.set_checkerboard_func(draw_checkerboard);
    checkerboard_paint_.setColor(checkerboard_color_);
  }

  /**
   * @brief Use no raster cache in the preroll_context() and
   * paint_context() structures.
   *
   * This method must be called before using the preroll_context() and
   * paint_context() structures in calls to the Layer::Preroll() and
   * Layer::Paint() methods. This is the default mode of operation.
   *
   * @see use_mock_raster_cache()
   * @see use_skia_raster_cache()
   */
  void use_null_raster_cache() { set_raster_cache_(nullptr); }

  /**
   * @brief Use a mock raster cache in the preroll_context() and
   * paint_context() structures.
   *
   * This method must be called before using the preroll_context() and
   * paint_context() structures in calls to the Layer::Preroll() and
   * Layer::Paint() methods. The mock raster cache behaves like a normal
   * raster cache with respect to decisions about when layers and pictures
   * should be cached, but it does not incur the overhead of rendering the
   * layers or caching the resulting pixels.
   *
   * @see use_null_raster_cache()
   * @see use_skia_raster_cache()
   */
  void use_mock_raster_cache() {
    set_raster_cache_(std::make_unique<MockRasterCache>());
  }

  /**
   * @brief Use a normal raster cache in the preroll_context() and
   * paint_context() structures.
   *
   * This method must be called before using the preroll_context() and
   * paint_context() structures in calls to the Layer::Preroll() and
   * Layer::Paint() methods. The Skia raster cache will behave identically
   * to the raster cache typically used when handling a frame on a device
   * including rendering the contents of pictures and layers to an
   * SkImage, but using a software rather than a hardware renderer.
   *
   * @see use_null_raster_cache()
   * @see use_mock_raster_cache()
   */
  void use_skia_raster_cache() {
    set_raster_cache_(std::make_unique<RasterCache>());
  }

  std::vector<RasterCacheItem*>& cacheable_items() { return cacheable_items_; }

  std::shared_ptr<TextureRegistry> texture_registry() {
    return texture_registry_;
  }
  RasterCache* raster_cache() { return raster_cache_.get(); }
  PrerollContext* preroll_context() { return &preroll_context_; }
  PaintContext& paint_context() { return paint_context_; }
  PaintContext& display_list_paint_context() {
    return display_list_paint_context_;
  }
  const SkPaint& checkerboard_paint() { return checkerboard_paint_; }
  PaintContext& checkerboard_context() { return checkerboard_context_; }
  LayerSnapshotStore& layer_snapshot_store() { return snapshot_store_; }

  sk_sp<DisplayList> display_list() {
    if (display_list_ == nullptr) {
      display_list_ = display_list_recorder_.Build();
      // null out the canvas and recorder fields of the PaintContext
      // to prevent future use.
      display_list_paint_context_.canvas = nullptr;
      display_list_paint_context_.builder = nullptr;
    }
    return display_list_;
  }

  void enable_leaf_layer_tracing() {
    paint_context_.enable_leaf_layer_tracing = true;
    paint_context_.layer_snapshot_store = &snapshot_store_;
  }

  void disable_leaf_layer_tracing() {
    paint_context_.enable_leaf_layer_tracing = false;
    paint_context_.layer_snapshot_store = nullptr;
  }

 private:
  void set_raster_cache_(std::unique_ptr<RasterCache> raster_cache) {
    raster_cache_ = std::move(raster_cache);
    preroll_context_.raster_cache = raster_cache_.get();
    paint_context_.raster_cache = raster_cache_.get();
    display_list_paint_context_.raster_cache = raster_cache_.get();
  }

  static constexpr SkColor checkerboard_color_ = 0x42424242;

  static void draw_checkerboard(SkCanvas* canvas,
                                DisplayListBuilder* builder,
                                const SkRect& rect) {
    if (canvas) {
      SkPaint paint;
      paint.setColor(checkerboard_color_);
      canvas->drawRect(rect, paint);
    }
    if (builder) {
      DlPaint paint;
      paint.setColor(checkerboard_color_);
      builder->drawRect(rect, paint);
    }
  }

  LayerStateStack preroll_state_stack_;
  LayerStateStack paint_state_stack_;
  LayerStateStack checkerboard_state_stack_;
  FixedRefreshRateStopwatch raster_time_;
  FixedRefreshRateStopwatch ui_time_;
  MutatorsStack mutators_stack_;
  std::shared_ptr<TextureRegistry> texture_registry_;

  std::unique_ptr<RasterCache> raster_cache_;
  PrerollContext preroll_context_;
  PaintContext paint_context_;
  DisplayListCanvasRecorder display_list_recorder_;
  LayerStateStack display_list_state_stack_;
  sk_sp<DisplayList> display_list_;
  PaintContext display_list_paint_context_;
  SkPaint checkerboard_paint_;
  PaintContext checkerboard_context_;
  LayerSnapshotStore snapshot_store_;

  std::vector<RasterCacheItem*> cacheable_items_;

  FML_DISALLOW_COPY_AND_ASSIGN(LayerTestBase);
};
using LayerTest = LayerTestBase<::testing::Test>;

}  // namespace testing
}  // namespace flutter

#endif  // FLOW_TESTING_LAYER_TEST_H_
