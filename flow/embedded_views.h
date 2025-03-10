// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLOW_EMBEDDED_VIEWS_H_
#define FLUTTER_FLOW_EMBEDDED_VIEWS_H_

#include <vector>

#include "flutter/display_list/display_list_builder.h"
#include "flutter/flow/rtree.h"
#include "flutter/flow/surface_frame.h"
#include "flutter/fml/memory/ref_counted.h"
#include "flutter/fml/raster_thread_merger.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace flutter {

enum MutatorType {
  kClipRect,
  kClipRRect,
  kClipPath,
  kTransform,
  kOpacity,
  kBackdropFilter
};

// Stores mutation information like clipping or kTransform.
//
// The `type` indicates the type of the mutation: kClipRect, kTransform and etc.
// Each `type` is paired with an object that supports the mutation. For example,
// if the `type` is kClipRect, `rect()` is used the represent the rect to be
// clipped. One mutation object must only contain one type of mutation.
class Mutator {
 public:
  Mutator(const Mutator& other) {
    type_ = other.type_;
    switch (other.type_) {
      case kClipRect:
        rect_ = other.rect_;
        break;
      case kClipRRect:
        rrect_ = other.rrect_;
        break;
      case kClipPath:
        path_ = new SkPath(*other.path_);
        break;
      case kTransform:
        matrix_ = other.matrix_;
        break;
      case kOpacity:
        alpha_ = other.alpha_;
        break;
      case kBackdropFilter:
        filter_ = other.filter_;
        break;
      default:
        break;
    }
  }

  explicit Mutator(const SkRect& rect) : type_(kClipRect), rect_(rect) {}
  explicit Mutator(const SkRRect& rrect) : type_(kClipRRect), rrect_(rrect) {}
  explicit Mutator(const SkPath& path)
      : type_(kClipPath), path_(new SkPath(path)) {}
  explicit Mutator(const SkMatrix& matrix)
      : type_(kTransform), matrix_(matrix) {}
  explicit Mutator(const int& alpha) : type_(kOpacity), alpha_(alpha) {}
  explicit Mutator(std::shared_ptr<const DlImageFilter> filter)
      : type_(kBackdropFilter), filter_(filter) {}

  const MutatorType& GetType() const { return type_; }
  const SkRect& GetRect() const { return rect_; }
  const SkRRect& GetRRect() const { return rrect_; }
  const SkPath& GetPath() const { return *path_; }
  const SkMatrix& GetMatrix() const { return matrix_; }
  const DlImageFilter& GetFilter() const { return *filter_; }
  const int& GetAlpha() const { return alpha_; }
  float GetAlphaFloat() const { return (alpha_ / 255.0); }

  bool operator==(const Mutator& other) const {
    if (type_ != other.type_) {
      return false;
    }
    switch (type_) {
      case kClipRect:
        return rect_ == other.rect_;
      case kClipRRect:
        return rrect_ == other.rrect_;
      case kClipPath:
        return *path_ == *other.path_;
      case kTransform:
        return matrix_ == other.matrix_;
      case kOpacity:
        return alpha_ == other.alpha_;
      case kBackdropFilter:
        return *filter_ == *other.filter_;
    }

    return false;
  }

  bool operator!=(const Mutator& other) const { return !operator==(other); }

  bool IsClipType() {
    return type_ == kClipRect || type_ == kClipRRect || type_ == kClipPath;
  }

  ~Mutator() {
    if (type_ == kClipPath) {
      delete path_;
    }
  };

 private:
  MutatorType type_;

  // TODO(cyanglaz): Remove union.
  //  https://github.com/flutter/flutter/issues/108470
  union {
    SkRect rect_;
    SkRRect rrect_;
    SkMatrix matrix_;
    SkPath* path_;
    int alpha_;
  };

  std::shared_ptr<const DlImageFilter> filter_;

};  // Mutator

// A stack of mutators that can be applied to an embedded platform view.
//
// The stack may include mutators like transforms and clips, each mutator
// applies to all the mutators that are below it in the stack and to the
// embedded view.
//
// For example consider the following stack: [T1, T2, T3], where T1 is the top
// of the stack and T3 is the bottom of the stack. Applying this mutators stack
// to a platform view P1 will result in T1(T2(T3(P1))).
class MutatorsStack {
 public:
  MutatorsStack() = default;

  void PushClipRect(const SkRect& rect);
  void PushClipRRect(const SkRRect& rrect);
  void PushClipPath(const SkPath& path);
  void PushTransform(const SkMatrix& matrix);
  void PushOpacity(const int& alpha);
  void PushBackdropFilter(const std::shared_ptr<const DlImageFilter>& filter);

  // Removes the `Mutator` on the top of the stack
  // and destroys it.
  void Pop();

  void PopTo(size_t stack_count);

  // Returns a reverse iterator pointing to the top of the stack, which is the
  // mutator that is furtherest from the leaf node.
  const std::vector<std::shared_ptr<Mutator>>::const_reverse_iterator Top()
      const;
  // Returns a reverse iterator pointing to the bottom of the stack, which is
  // the mutator that is closeset from the leaf node.
  const std::vector<std::shared_ptr<Mutator>>::const_reverse_iterator Bottom()
      const;

  // Returns an iterator pointing to the beginning of the mutator vector, which
  // is the mutator that is furtherest from the leaf node.
  const std::vector<std::shared_ptr<Mutator>>::const_iterator Begin() const;

  // Returns an iterator pointing to the end of the mutator vector, which is the
  // mutator that is closest from the leaf node.
  const std::vector<std::shared_ptr<Mutator>>::const_iterator End() const;

  bool is_empty() const { return vector_.empty(); }
  size_t stack_count() const { return vector_.size(); }

  bool operator==(const MutatorsStack& other) const {
    if (vector_.size() != other.vector_.size()) {
      return false;
    }
    for (size_t i = 0; i < vector_.size(); i++) {
      if (*vector_[i] != *other.vector_[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator==(const std::vector<Mutator>& other) const {
    if (vector_.size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < vector_.size(); i++) {
      if (*vector_[i] != other[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const MutatorsStack& other) const {
    return !operator==(other);
  }

  bool operator!=(const std::vector<Mutator>& other) const {
    return !operator==(other);
  }

 private:
  std::vector<std::shared_ptr<Mutator>> vector_;
};  // MutatorsStack

class EmbeddedViewParams {
 public:
  EmbeddedViewParams() = default;

  EmbeddedViewParams(SkMatrix matrix,
                     SkSize size_points,
                     MutatorsStack mutators_stack,
                     bool display_list_enabled = false)
      : matrix_(matrix),
        size_points_(size_points),
        mutators_stack_(mutators_stack),
        display_list_enabled_(display_list_enabled) {
    SkPath path;
    SkRect starting_rect = SkRect::MakeSize(size_points);
    path.addRect(starting_rect);
    path.transform(matrix);
    final_bounding_rect_ = path.getBounds();
  }

  // The transformation Matrix corresponding to the sum of all the
  // transformations in the platform view's mutator stack.
  const SkMatrix& transformMatrix() const { return matrix_; };
  // The original size of the platform view before any mutation matrix is
  // applied.
  const SkSize& sizePoints() const { return size_points_; };
  // The mutators stack contains the detailed step by step mutations for this
  // platform view.
  const MutatorsStack& mutatorsStack() const { return mutators_stack_; };
  // The bounding rect of the platform view after applying all the mutations.
  //
  // Clippings are ignored.
  const SkRect& finalBoundingRect() const { return final_bounding_rect_; }

  // Pushes the stored DlImageFilter object to the mutators stack.
  void PushImageFilter(std::shared_ptr<const DlImageFilter> filter) {
    mutators_stack_.PushBackdropFilter(filter);
  }

  // Whether the embedder should construct DisplayList objects to hold the
  // rendering commands for each between-view slice of the layer tree.
  bool display_list_enabled() const { return display_list_enabled_; }

  bool operator==(const EmbeddedViewParams& other) const {
    return size_points_ == other.size_points_ &&
           mutators_stack_ == other.mutators_stack_ &&
           final_bounding_rect_ == other.final_bounding_rect_ &&
           matrix_ == other.matrix_;
  }

 private:
  SkMatrix matrix_;
  SkSize size_points_;
  MutatorsStack mutators_stack_;
  SkRect final_bounding_rect_;
  bool display_list_enabled_;
};

enum class PostPrerollResult {
  // Frame has successfully rasterized.
  kSuccess,
  // Frame is submitted twice. This is currently only used when
  // thread configuration change occurs.
  kResubmitFrame,
  // Frame is dropped and a new frame with the same layer tree is
  // attempted. This is currently only used when thread configuration
  // change occurs.
  kSkipAndRetryFrame
};

// The |PlatformViewLayer| calls |CompositeEmbeddedView| in its |Paint|
// method to replace the leaf_nodes_canvas and leaf_nodes_builder in its
// |PaintContext| for subsequent layers in the frame to render into.
// The builder value will only be supplied if the associated ScopedFrame
// is being rendered to DisplayLists. The |EmbedderPaintContext| struct
// allows the method to return both values.
struct EmbedderPaintContext {
  SkCanvas* canvas;
  DisplayListBuilder* builder;
};

// The |EmbedderViewSlice| represents the details of recording all of
// the layer tree rendering operations that appear between before, after
// and between the embedded views. The Slice may be recorded into an
// SkPicture or a DisplayListBuilder depending on the ScopedFrame.
class EmbedderViewSlice {
 public:
  virtual ~EmbedderViewSlice() = default;
  virtual SkCanvas* canvas() = 0;
  virtual DisplayListBuilder* builder() = 0;
  virtual void end_recording() = 0;
  virtual std::list<SkRect> searchNonOverlappingDrawnRects(
      const SkRect& query) const = 0;
  virtual void render_into(SkCanvas* canvas) = 0;
  virtual void render_into(DisplayListBuilder* builder) = 0;
};

class SkPictureEmbedderViewSlice : public EmbedderViewSlice {
 public:
  SkPictureEmbedderViewSlice(SkRect view_bounds);
  ~SkPictureEmbedderViewSlice() override = default;

  SkCanvas* canvas() override;
  DisplayListBuilder* builder() override;
  void end_recording() override;
  std::list<SkRect> searchNonOverlappingDrawnRects(
      const SkRect& query) const override;
  void render_into(SkCanvas* canvas) override;
  void render_into(DisplayListBuilder* builder) override;

 private:
  std::unique_ptr<SkPictureRecorder> recorder_;
  sk_sp<RTree> rtree_;
  sk_sp<SkPicture> picture_;
};

class DisplayListEmbedderViewSlice : public EmbedderViewSlice {
 public:
  DisplayListEmbedderViewSlice(SkRect view_bounds);
  ~DisplayListEmbedderViewSlice() override = default;

  SkCanvas* canvas() override;
  DisplayListBuilder* builder() override;
  void end_recording() override;
  std::list<SkRect> searchNonOverlappingDrawnRects(
      const SkRect& query) const override;
  void render_into(SkCanvas* canvas) override;
  void render_into(DisplayListBuilder* builder) override;

 private:
  std::unique_ptr<DisplayListCanvasRecorder> recorder_;
  sk_sp<DisplayList> display_list_;
};

// Facilitates embedding of platform views within the flow layer tree.
//
// Used on iOS, Android (hybrid composite mode), and on embedded platforms
// that provide a system compositor as part of the project arguments.
class ExternalViewEmbedder {
  // TODO(cyanglaz): Make embedder own the `EmbeddedViewParams`.

 public:
  ExternalViewEmbedder() = default;

  virtual ~ExternalViewEmbedder() = default;

  // Usually, the root canvas is not owned by the view embedder. However, if
  // the view embedder wants to provide a canvas to the rasterizer, it may
  // return one here. This canvas takes priority over the canvas materialized
  // from the on-screen render target.
  virtual SkCanvas* GetRootCanvas() = 0;

  // Call this in-lieu of |SubmitFrame| to clear pre-roll state and
  // sets the stage for the next pre-roll.
  virtual void CancelFrame() = 0;

  // Indicates the beginning of a frame.
  //
  // The `raster_thread_merger` will be null if |SupportsDynamicThreadMerging|
  // returns false.
  virtual void BeginFrame(
      SkISize frame_size,
      GrDirectContext* context,
      double device_pixel_ratio,
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) = 0;

  virtual void PrerollCompositeEmbeddedView(
      int view_id,
      std::unique_ptr<EmbeddedViewParams> params) = 0;

  // This needs to get called after |Preroll| finishes on the layer tree.
  // Returns kResubmitFrame if the frame needs to be processed again, this is
  // after it does any requisite tasks needed to bring itself to a valid state.
  // Returns kSuccess if the view embedder is already in a valid state.
  virtual PostPrerollResult PostPrerollAction(
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
    return PostPrerollResult::kSuccess;
  }

  virtual std::vector<SkCanvas*> GetCurrentCanvases() = 0;
  virtual std::vector<DisplayListBuilder*> GetCurrentBuilders() = 0;

  // Must be called on the UI thread.
  virtual EmbedderPaintContext CompositeEmbeddedView(int view_id) = 0;

  // Implementers must submit the frame by calling frame.Submit().
  //
  // This method can mutate the root Skia canvas before submitting the frame.
  //
  // It can also allocate frames for overlay surfaces to compose hybrid views.
  virtual void SubmitFrame(GrDirectContext* context,
                           std::unique_ptr<SurfaceFrame> frame);

  // This method provides the embedder a way to do additional tasks after
  // |SubmitFrame|. For example, merge task runners if `should_resubmit_frame`
  // is true.
  //
  // For example on the iOS embedder, threads are merged in this call.
  // A new frame on the platform thread starts immediately. If the GPU thread
  // still has some task running, there could be two frames being rendered
  // concurrently, which causes undefined behaviors.
  //
  // The `raster_thread_merger` will be null if |SupportsDynamicThreadMerging|
  // returns false.
  virtual void EndFrame(
      bool should_resubmit_frame,
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {}

  // Whether the embedder should support dynamic thread merging.
  //
  // Returning `true` results a |RasterThreadMerger| instance to be created.
  // * See also |BegineFrame| and |EndFrame| for getting the
  // |RasterThreadMerger| instance.
  virtual bool SupportsDynamicThreadMerging();

  // Called when the rasterizer is being torn down.
  // This method provides a way to release resources associated with the current
  // embedder.
  virtual void Teardown();

  // Change the flag about whether it is used in this frame, it will be set to
  // true when 'BeginFrame' and false when 'EndFrame'.
  void SetUsedThisFrame(bool used_this_frame) {
    used_this_frame_ = used_this_frame;
  }

  // Whether it is used in this frame, returns true between 'BeginFrame' and
  // 'EndFrame', otherwise returns false.
  bool GetUsedThisFrame() const { return used_this_frame_; }

  // Pushes the platform view id of a visited platform view to a list of
  // visited platform views.
  virtual void PushVisitedPlatformView(int64_t view_id) {}

  // Pushes a DlImageFilter object to each platform view within a list of
  // visited platform views.
  //
  // See also: |PushVisitedPlatformView| for pushing platform view ids to the
  // visited platform views list.
  virtual void PushFilterToVisitedPlatformViews(
      std::shared_ptr<const DlImageFilter> filter) {}

 private:
  bool used_this_frame_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(ExternalViewEmbedder);

};  // ExternalViewEmbedder

}  // namespace flutter

#endif  // FLUTTER_FLOW_EMBEDDED_VIEWS_H_
