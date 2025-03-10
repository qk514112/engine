// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/flow/layers/platform_view_layer.h"

namespace flutter {

PlatformViewLayer::PlatformViewLayer(const SkPoint& offset,
                                     const SkSize& size,
                                     int64_t view_id)
    : offset_(offset), size_(size), view_id_(view_id) {}

void PlatformViewLayer::Preroll(PrerollContext* context) {
  set_paint_bounds(SkRect::MakeXYWH(offset_.x(), offset_.y(), size_.width(),
                                    size_.height()));

  if (context->view_embedder == nullptr) {
    FML_LOG(ERROR) << "Trying to embed a platform view but the PrerollContext "
                      "does not support embedding";
    return;
  }
  context->has_platform_view = true;
  set_subtree_has_platform_view(true);
  auto mutators = context->state_stack.mutators_delegate();
  std::unique_ptr<EmbeddedViewParams> params =
      std::make_unique<EmbeddedViewParams>(context->state_stack.transform_3x3(),
                                           size_, *mutators,
                                           context->display_list_enabled);
  context->view_embedder->PrerollCompositeEmbeddedView(view_id_,
                                                       std::move(params));
  context->view_embedder->PushVisitedPlatformView(view_id_);
}

void PlatformViewLayer::Paint(PaintContext& context) const {
  if (context.view_embedder == nullptr) {
    FML_LOG(ERROR) << "Trying to embed a platform view but the PaintContext "
                      "does not support embedding";
    return;
  }
  EmbedderPaintContext embedder_context =
      context.view_embedder->CompositeEmbeddedView(view_id_);
  context.canvas = embedder_context.canvas;
  context.builder = embedder_context.builder;
  if (context.builder) {
    context.state_stack.set_delegate(context.builder);
  } else {
    context.state_stack.set_delegate(context.canvas);
  }
}

}  // namespace flutter
