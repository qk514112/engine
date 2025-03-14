// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/display_list/display_list_dispatcher.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "display_list/display_list_blend_mode.h"
#include "display_list/display_list_color_filter.h"
#include "display_list/display_list_color_source.h"
#include "display_list/display_list_path_effect.h"
#include "display_list/display_list_tile_mode.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/trace_event.h"
#include "impeller/display_list/display_list_image_impeller.h"
#include "impeller/display_list/nine_patch_converter.h"
#include "impeller/display_list/vertices_converter.h"
#include "impeller/entity/contents/filters/filter_contents.h"
#include "impeller/entity/contents/filters/inputs/filter_input.h"
#include "impeller/entity/contents/linear_gradient_contents.h"
#include "impeller/entity/contents/radial_gradient_contents.h"
#include "impeller/entity/contents/runtime_effect_contents.h"
#include "impeller/entity/contents/sweep_gradient_contents.h"
#include "impeller/entity/contents/tiled_texture_contents.h"
#include "impeller/entity/entity.h"
#include "impeller/entity/geometry.h"
#include "impeller/geometry/path.h"
#include "impeller/geometry/path_builder.h"
#include "impeller/geometry/scalar.h"
#include "impeller/geometry/sigma.h"
#include "impeller/geometry/vertices.h"
#include "impeller/renderer/formats.h"
#include "impeller/typographer/backends/skia/text_frame_skia.h"

#include "third_party/skia/include/core/SkColor.h"

namespace impeller {

#define UNIMPLEMENTED \
  FML_DLOG(ERROR) << "Unimplemented detail in " << __FUNCTION__;

DisplayListDispatcher::DisplayListDispatcher() = default;

DisplayListDispatcher::~DisplayListDispatcher() = default;

static BlendMode ToBlendMode(flutter::DlBlendMode mode) {
  switch (mode) {
    case flutter::DlBlendMode::kClear:
      return BlendMode::kClear;
    case flutter::DlBlendMode::kSrc:
      return BlendMode::kSource;
    case flutter::DlBlendMode::kDst:
      return BlendMode::kDestination;
    case flutter::DlBlendMode::kSrcOver:
      return BlendMode::kSourceOver;
    case flutter::DlBlendMode::kDstOver:
      return BlendMode::kDestinationOver;
    case flutter::DlBlendMode::kSrcIn:
      return BlendMode::kSourceIn;
    case flutter::DlBlendMode::kDstIn:
      return BlendMode::kDestinationIn;
    case flutter::DlBlendMode::kSrcOut:
      return BlendMode::kSourceOut;
    case flutter::DlBlendMode::kDstOut:
      return BlendMode::kDestinationOut;
    case flutter::DlBlendMode::kSrcATop:
      return BlendMode::kSourceATop;
    case flutter::DlBlendMode::kDstATop:
      return BlendMode::kDestinationATop;
    case flutter::DlBlendMode::kXor:
      return BlendMode::kXor;
    case flutter::DlBlendMode::kPlus:
      return BlendMode::kPlus;
    case flutter::DlBlendMode::kModulate:
      return BlendMode::kModulate;
    case flutter::DlBlendMode::kScreen:
      return BlendMode::kScreen;
    case flutter::DlBlendMode::kOverlay:
      return BlendMode::kOverlay;
    case flutter::DlBlendMode::kDarken:
      return BlendMode::kDarken;
    case flutter::DlBlendMode::kLighten:
      return BlendMode::kLighten;
    case flutter::DlBlendMode::kColorDodge:
      return BlendMode::kColorDodge;
    case flutter::DlBlendMode::kColorBurn:
      return BlendMode::kColorBurn;
    case flutter::DlBlendMode::kHardLight:
      return BlendMode::kHardLight;
    case flutter::DlBlendMode::kSoftLight:
      return BlendMode::kSoftLight;
    case flutter::DlBlendMode::kDifference:
      return BlendMode::kDifference;
    case flutter::DlBlendMode::kExclusion:
      return BlendMode::kExclusion;
    case flutter::DlBlendMode::kMultiply:
      return BlendMode::kMultiply;
    case flutter::DlBlendMode::kHue:
      return BlendMode::kHue;
    case flutter::DlBlendMode::kSaturation:
      return BlendMode::kSaturation;
    case flutter::DlBlendMode::kColor:
      return BlendMode::kColor;
    case flutter::DlBlendMode::kLuminosity:
      return BlendMode::kLuminosity;
  }
  FML_UNREACHABLE();
}

static Entity::TileMode ToTileMode(flutter::DlTileMode tile_mode) {
  switch (tile_mode) {
    case flutter::DlTileMode::kClamp:
      return Entity::TileMode::kClamp;
    case flutter::DlTileMode::kRepeat:
      return Entity::TileMode::kRepeat;
    case flutter::DlTileMode::kMirror:
      return Entity::TileMode::kMirror;
    case flutter::DlTileMode::kDecal:
      return Entity::TileMode::kDecal;
  }
}

static impeller::SamplerDescriptor ToSamplerDescriptor(
    const flutter::DlImageSampling options) {
  impeller::SamplerDescriptor desc;
  switch (options) {
    case flutter::DlImageSampling::kNearestNeighbor:
      desc.min_filter = desc.mag_filter = impeller::MinMagFilter::kNearest;
      desc.label = "Nearest Sampler";
      break;
    case flutter::DlImageSampling::kLinear:
      desc.min_filter = desc.mag_filter = impeller::MinMagFilter::kLinear;
      desc.label = "Linear Sampler";
      break;
    case flutter::DlImageSampling::kMipmapLinear:
      desc.min_filter = desc.mag_filter = impeller::MinMagFilter::kLinear;
      desc.mip_filter = impeller::MipFilter::kLinear;
      desc.label = "Mipmap Linear Sampler";
      break;
    default:
      break;
  }
  return desc;
}

static impeller::SamplerDescriptor ToSamplerDescriptor(
    const flutter::DlFilterMode options) {
  impeller::SamplerDescriptor desc;
  switch (options) {
    case flutter::DlFilterMode::kNearest:
      desc.min_filter = desc.mag_filter = impeller::MinMagFilter::kNearest;
      desc.label = "Nearest Sampler";
      break;
    case flutter::DlFilterMode::kLinear:
      desc.min_filter = desc.mag_filter = impeller::MinMagFilter::kLinear;
      desc.label = "Linear Sampler";
      break;
    default:
      break;
  }
  return desc;
}

static Matrix ToMatrix(const SkMatrix& m) {
  return Matrix{
      // clang-format off
      m[0], m[3], 0, m[6],
      m[1], m[4], 0, m[7],
      0,    0,    1, 0,
      m[2], m[5], 0, m[8],
      // clang-format on
  };
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setAntiAlias(bool aa) {
  // Nothing to do because AA is implicit.
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setDither(bool dither) {}

static Paint::Style ToStyle(flutter::DlDrawStyle style) {
  switch (style) {
    case flutter::DlDrawStyle::kFill:
      return Paint::Style::kFill;
    case flutter::DlDrawStyle::kStroke:
      return Paint::Style::kStroke;
    case flutter::DlDrawStyle::kStrokeAndFill:
      UNIMPLEMENTED;
      break;
  }
  return Paint::Style::kFill;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setStyle(flutter::DlDrawStyle style) {
  paint_.style = ToStyle(style);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setColor(flutter::DlColor color) {
  paint_.color = {
      color.getRedF(),
      color.getGreenF(),
      color.getBlueF(),
      color.getAlphaF(),
  };
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setStrokeWidth(SkScalar width) {
  paint_.stroke_width = width;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setStrokeMiter(SkScalar limit) {
  paint_.stroke_miter = limit;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setStrokeCap(flutter::DlStrokeCap cap) {
  switch (cap) {
    case flutter::DlStrokeCap::kButt:
      paint_.stroke_cap = Cap::kButt;
      break;
    case flutter::DlStrokeCap::kRound:
      paint_.stroke_cap = Cap::kRound;
      break;
    case flutter::DlStrokeCap::kSquare:
      paint_.stroke_cap = Cap::kSquare;
      break;
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setStrokeJoin(flutter::DlStrokeJoin join) {
  switch (join) {
    case flutter::DlStrokeJoin::kMiter:
      paint_.stroke_join = Join::kMiter;
      break;
    case flutter::DlStrokeJoin::kRound:
      paint_.stroke_join = Join::kRound;
      break;
    case flutter::DlStrokeJoin::kBevel:
      paint_.stroke_join = Join::kBevel;
      break;
  }
}

static Point ToPoint(const SkPoint& point) {
  return Point::MakeXY(point.fX, point.fY);
}

static Color ToColor(const SkColor& color) {
  return {
      static_cast<Scalar>(SkColorGetR(color) / 255.0),  //
      static_cast<Scalar>(SkColorGetG(color) / 255.0),  //
      static_cast<Scalar>(SkColorGetB(color) / 255.0),  //
      static_cast<Scalar>(SkColorGetA(color) / 255.0)   //
  };
}

static std::vector<Color> ToColors(const flutter::DlColor colors[], int count) {
  auto result = std::vector<Color>();
  if (colors == nullptr) {
    return result;
  }
  for (int i = 0; i < count; i++) {
    result.push_back(ToColor(colors[i]));
  }
  return result;
}

static std::vector<Matrix> ToRSXForms(const SkRSXform xform[], int count) {
  auto result = std::vector<Matrix>();
  for (int i = 0; i < count; i++) {
    auto form = xform[i];
    // clang-format off
    auto matrix = Matrix{
      form.fSCos, form.fSSin, 0, 0,
     -form.fSSin, form.fSCos, 0, 0,
      0,          0,          1, 0,
      form.fTx,   form.fTy,   0, 1
    };
    // clang-format on
    result.push_back(matrix);
  }
  return result;
}

// Convert display list colors + stops into impeller colors and stops, taking
// care to ensure that the stops always start with 0.0 and end with 1.0.
template <typename T>
static void ConvertStops(T* gradient,
                         std::vector<Color>* colors,
                         std::vector<float>* stops) {
  FML_DCHECK(gradient->stop_count() >= 2);

  auto* dl_colors = gradient->colors();
  auto* dl_stops = gradient->stops();
  if (dl_stops[0] != 0.0) {
    colors->emplace_back(ToColor(dl_colors[0]));
    stops->emplace_back(0);
  }
  for (auto i = 0; i < gradient->stop_count(); i++) {
    colors->emplace_back(ToColor(dl_colors[i]));
    stops->emplace_back(dl_stops[i]);
  }
  if (stops->back() != 1.0) {
    colors->emplace_back(colors->back());
    stops->emplace_back(1.0);
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setColorSource(
    const flutter::DlColorSource* source) {
  if (!source) {
    paint_.color_source = std::nullopt;
    return;
  }

  switch (source->type()) {
    case flutter::DlColorSourceType::kColor: {
      const flutter::DlColorColorSource* color = source->asColor();
      paint_.color_source = std::nullopt;
      setColor(color->color());
      FML_DCHECK(color);
      return;
    }
    case flutter::DlColorSourceType::kLinearGradient: {
      const flutter::DlLinearGradientColorSource* linear =
          source->asLinearGradient();
      FML_DCHECK(linear);
      auto start_point = ToPoint(linear->start_point());
      auto end_point = ToPoint(linear->end_point());
      std::vector<Color> colors;
      std::vector<float> stops;
      ConvertStops(linear, &colors, &stops);

      auto tile_mode = ToTileMode(linear->tile_mode());
      auto matrix = ToMatrix(linear->matrix());
      paint_.color_source = [start_point, end_point, colors = std::move(colors),
                             stops = std::move(stops), tile_mode, matrix]() {
        auto contents = std::make_shared<LinearGradientContents>();
        contents->SetColors(colors);
        contents->SetStops(stops);
        contents->SetEndPoints(start_point, end_point);
        contents->SetTileMode(tile_mode);
        contents->SetMatrix(matrix);
        return contents;
      };
      return;
    }
    case flutter::DlColorSourceType::kRadialGradient: {
      const flutter::DlRadialGradientColorSource* radialGradient =
          source->asRadialGradient();
      FML_DCHECK(radialGradient);
      auto center = ToPoint(radialGradient->center());
      auto radius = radialGradient->radius();
      std::vector<Color> colors;
      std::vector<float> stops;
      ConvertStops(radialGradient, &colors, &stops);

      auto tile_mode = ToTileMode(radialGradient->tile_mode());
      auto matrix = ToMatrix(radialGradient->matrix());
      paint_.color_source = [center, radius, colors = std::move(colors),
                             stops = std::move(stops), tile_mode, matrix]() {
        auto contents = std::make_shared<RadialGradientContents>();
        contents->SetColors(colors);
        contents->SetStops(stops);
        contents->SetCenterAndRadius(center, radius);
        contents->SetTileMode(tile_mode);
        contents->SetMatrix(matrix);
        return contents;
      };
      return;
    }
    case flutter::DlColorSourceType::kSweepGradient: {
      const flutter::DlSweepGradientColorSource* sweepGradient =
          source->asSweepGradient();
      FML_DCHECK(sweepGradient);

      auto center = ToPoint(sweepGradient->center());
      auto start_angle = Degrees(sweepGradient->start());
      auto end_angle = Degrees(sweepGradient->end());
      std::vector<Color> colors;
      std::vector<float> stops;
      ConvertStops(sweepGradient, &colors, &stops);

      auto tile_mode = ToTileMode(sweepGradient->tile_mode());
      auto matrix = ToMatrix(sweepGradient->matrix());
      paint_.color_source = [center, start_angle, end_angle,
                             colors = std::move(colors),
                             stops = std::move(stops), tile_mode, matrix]() {
        auto contents = std::make_shared<SweepGradientContents>();
        contents->SetCenterAndAngles(center, start_angle, end_angle);
        contents->SetColors(colors);
        contents->SetStops(stops);
        contents->SetTileMode(tile_mode);
        contents->SetMatrix(matrix);
        return contents;
      };
      return;
    }
    case flutter::DlColorSourceType::kImage: {
      const flutter::DlImageColorSource* image_color_source = source->asImage();
      FML_DCHECK(image_color_source &&
                 image_color_source->image()->impeller_texture());
      auto texture = image_color_source->image()->impeller_texture();
      auto x_tile_mode = ToTileMode(image_color_source->horizontal_tile_mode());
      auto y_tile_mode = ToTileMode(image_color_source->vertical_tile_mode());
      auto desc = ToSamplerDescriptor(image_color_source->sampling());
      auto matrix = ToMatrix(image_color_source->matrix());
      paint_.color_source = [texture, x_tile_mode, y_tile_mode, desc,
                             matrix]() {
        auto contents = std::make_shared<TiledTextureContents>();
        contents->SetTexture(texture);
        contents->SetTileModes(x_tile_mode, y_tile_mode);
        contents->SetSamplerDescriptor(desc);
        contents->SetMatrix(matrix);
        return contents;
      };
      return;
    }
    case flutter::DlColorSourceType::kRuntimeEffect: {
      const flutter::DlRuntimeEffectColorSource* runtime_effect_color_source =
          source->asRuntimeEffect();
      auto runtime_stage =
          runtime_effect_color_source->runtime_effect()->runtime_stage();
      auto uniform_data = runtime_effect_color_source->uniform_data();
      auto samplers = runtime_effect_color_source->samplers();

      std::vector<RuntimeEffectContents::TextureInput> texture_inputs;

      for (auto& sampler : samplers) {
        auto* image = sampler->asImage();
        if (!sampler->asImage()) {
          UNIMPLEMENTED;
          return;
        }
        FML_DCHECK(image->image()->impeller_texture());
        texture_inputs.push_back({
            .sampler_descriptor = ToSamplerDescriptor(image->sampling()),
            .texture = image->image()->impeller_texture(),
        });
      }

      paint_.color_source = [runtime_stage, uniform_data, texture_inputs]() {
        auto contents = std::make_shared<RuntimeEffectContents>();
        contents->SetRuntimeStage(runtime_stage);
        contents->SetUniformData(uniform_data);
        contents->SetTextureInputs(texture_inputs);
        return contents;
      };
      return;
    }
    case flutter::DlColorSourceType::kConicalGradient:
    case flutter::DlColorSourceType::kUnknown:
      UNIMPLEMENTED;
      break;
  }

  // Needs https://github.com/flutter/flutter/issues/95434
  UNIMPLEMENTED;
}

static std::optional<Paint::ColorFilterProc> ToColorFilterProc(
    const flutter::DlColorFilter* filter) {
  if (filter == nullptr) {
    return std::nullopt;
  }
  switch (filter->type()) {
    case flutter::DlColorFilterType::kBlend: {
      auto dl_blend = filter->asBlend();
      auto blend_mode = ToBlendMode(dl_blend->mode());
      auto color = ToColor(dl_blend->color());
      return [blend_mode, color](FilterInput::Ref input) {
        return ColorFilterContents::MakeBlend(blend_mode, {std::move(input)},
                                              color);
      };
    }
    case flutter::DlColorFilterType::kMatrix: {
      const flutter::DlMatrixColorFilter* dl_matrix = filter->asMatrix();
      impeller::FilterContents::ColorMatrix color_matrix;
      dl_matrix->get_matrix(color_matrix.array);
      return [color_matrix](FilterInput::Ref input) {
        return ColorFilterContents::MakeColorMatrix({std::move(input)},
                                                    color_matrix);
      };
    }
    case flutter::DlColorFilterType::kSrgbToLinearGamma:
      return [](FilterInput::Ref input) {
        return ColorFilterContents::MakeSrgbToLinearFilter({std::move(input)});
      };
    case flutter::DlColorFilterType::kLinearToSrgbGamma:
      return [](FilterInput::Ref input) {
        return ColorFilterContents::MakeLinearToSrgbFilter({std::move(input)});
      };
    case flutter::DlColorFilterType::kUnknown:
      FML_LOG(ERROR) << "requested DlColorFilterType::kUnknown";
      UNIMPLEMENTED;
  }
  return std::nullopt;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setColorFilter(
    const flutter::DlColorFilter* filter) {
  // Needs https://github.com/flutter/flutter/issues/95434
  paint_.color_filter = ToColorFilterProc(filter);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setInvertColors(bool invert) {
  UNIMPLEMENTED;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setBlendMode(flutter::DlBlendMode dl_mode) {
  paint_.blend_mode = ToBlendMode(dl_mode);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setBlender(sk_sp<SkBlender> blender) {
  // Needs https://github.com/flutter/flutter/issues/95434
  UNIMPLEMENTED;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setPathEffect(const flutter::DlPathEffect* effect) {
  // Needs https://github.com/flutter/flutter/issues/95434
  UNIMPLEMENTED;
}

static FilterContents::BlurStyle ToBlurStyle(SkBlurStyle blur_style) {
  switch (blur_style) {
    case kNormal_SkBlurStyle:
      return FilterContents::BlurStyle::kNormal;
    case kSolid_SkBlurStyle:
      return FilterContents::BlurStyle::kSolid;
    case kOuter_SkBlurStyle:
      return FilterContents::BlurStyle::kOuter;
    case kInner_SkBlurStyle:
      return FilterContents::BlurStyle::kInner;
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setMaskFilter(const flutter::DlMaskFilter* filter) {
  // Needs https://github.com/flutter/flutter/issues/95434
  if (filter == nullptr) {
    paint_.mask_blur_descriptor = std::nullopt;
    return;
  }
  switch (filter->type()) {
    case flutter::DlMaskFilterType::kBlur: {
      auto blur = filter->asBlur();

      paint_.mask_blur_descriptor = {
          .style = ToBlurStyle(blur->style()),
          .sigma = Sigma(blur->sigma()),
      };
      break;
    }
    case flutter::DlMaskFilterType::kUnknown:
      UNIMPLEMENTED;
      break;
  }
}

static std::optional<Paint::ImageFilterProc> ToImageFilterProc(
    const flutter::DlImageFilter* filter) {
  if (filter == nullptr) {
    return std::nullopt;
  }

  switch (filter->type()) {
    case flutter::DlImageFilterType::kBlur: {
      auto blur = filter->asBlur();
      auto sigma_x = Sigma(blur->sigma_x());
      auto sigma_y = Sigma(blur->sigma_y());
      auto tile_mode = ToTileMode(blur->tile_mode());

      return [sigma_x, sigma_y, tile_mode](const FilterInput::Ref& input,
                                           const Matrix& effect_transform) {
        return FilterContents::MakeGaussianBlur(
            input, sigma_x, sigma_y, FilterContents::BlurStyle::kNormal,
            tile_mode, effect_transform);
      };

      break;
    }
    case flutter::DlImageFilterType::kDilate: {
      auto dilate = filter->asDilate();
      FML_DCHECK(dilate);
      if (dilate->radius_x() < 0 || dilate->radius_y() < 0) {
        return std::nullopt;
      }
      auto radius_x = Radius(dilate->radius_x());
      auto radius_y = Radius(dilate->radius_y());
      return [radius_x, radius_y](FilterInput::Ref input,
                                  const Matrix& effect_transform) {
        return FilterContents::MakeMorphology(
            std::move(input), radius_x, radius_y,
            FilterContents::MorphType::kDilate, effect_transform);
      };
      break;
    }
    case flutter::DlImageFilterType::kErode: {
      auto erode = filter->asErode();
      FML_DCHECK(erode);
      if (erode->radius_x() < 0 || erode->radius_y() < 0) {
        return std::nullopt;
      }
      auto radius_x = Radius(erode->radius_x());
      auto radius_y = Radius(erode->radius_y());
      return [radius_x, radius_y](FilterInput::Ref input,
                                  const Matrix& effect_transform) {
        return FilterContents::MakeMorphology(
            std::move(input), radius_x, radius_y,
            FilterContents::MorphType::kErode, effect_transform);
      };
      break;
    }
    case flutter::DlImageFilterType::kMatrix: {
      auto matrix_filter = filter->asMatrix();
      FML_DCHECK(matrix_filter);
      auto matrix = ToMatrix(matrix_filter->matrix());
      auto desc = ToSamplerDescriptor(matrix_filter->sampling());
      return [matrix, desc](FilterInput::Ref input,
                            const Matrix& effect_transform) {
        return FilterContents::MakeMatrixFilter(std::move(input), matrix, desc);
      };
      break;
    }
    case flutter::DlImageFilterType::kComposeFilter: {
      auto compose = filter->asCompose();
      FML_DCHECK(compose);
      auto outer = compose->outer();
      auto inner = compose->inner();
      auto outer_proc = ToImageFilterProc(outer.get());
      auto inner_proc = ToImageFilterProc(inner.get());
      if (!outer_proc.has_value()) {
        return inner_proc;
      }
      if (!inner_proc.has_value()) {
        return outer_proc;
      }
      FML_DCHECK(outer_proc.has_value() && inner_proc.has_value());
      return [outer_filter = outer_proc.value(),
              inner_filter = inner_proc.value()](
                 FilterInput::Ref input, const Matrix& effect_transform) {
        auto contents = inner_filter(std::move(input), effect_transform);
        contents = outer_filter(FilterInput::Make(contents), effect_transform);
        return contents;
      };
      break;
    }
    case flutter::DlImageFilterType::kColorFilter: {
      auto color_filter_image_filter = filter->asColorFilter();
      FML_DCHECK(color_filter_image_filter);
      auto color_filter_proc =
          ToColorFilterProc(color_filter_image_filter->color_filter().get());
      if (!color_filter_proc.has_value()) {
        return std::nullopt;
      }
      return [color_filter = color_filter_proc.value()](
                 FilterInput::Ref input, const Matrix& effect_transform) {
        return color_filter(std::move(input));
      };
      break;
    }
    case flutter::DlImageFilterType::kLocalMatrixFilter: {
      auto local_matrix_filter = filter->asLocalMatrix();
      FML_DCHECK(local_matrix_filter);
      auto internal_filter = local_matrix_filter->image_filter();
      FML_DCHECK(internal_filter);

      auto image_filter_proc = ToImageFilterProc(internal_filter.get());
      if (!image_filter_proc.has_value()) {
        return std::nullopt;
      }

      auto matrix = ToMatrix(local_matrix_filter->matrix());

      return [matrix, filter_proc = image_filter_proc.value()](
                 FilterInput::Ref input, const Matrix& effect_transform) {
        std::shared_ptr<FilterContents> filter =
            filter_proc(std::move(input), effect_transform);
        return FilterContents::MakeLocalMatrixFilter(FilterInput::Make(filter),
                                                     matrix);
      };
      break;
    }
    case flutter::DlImageFilterType::kUnknown:
      return std::nullopt;
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::setImageFilter(
    const flutter::DlImageFilter* filter) {
  paint_.image_filter = ToImageFilterProc(filter);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::save() {
  canvas_.Save();
}

static std::optional<Rect> ToRect(const SkRect* rect) {
  if (rect == nullptr) {
    return std::nullopt;
  }
  return Rect::MakeLTRB(rect->fLeft, rect->fTop, rect->fRight, rect->fBottom);
}

static std::vector<Rect> ToRects(const SkRect tex[], int count) {
  auto result = std::vector<Rect>();
  for (int i = 0; i < count; i++) {
    result.push_back(ToRect(&tex[i]).value());
  }
  return result;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::saveLayer(const SkRect* bounds,
                                      const flutter::SaveLayerOptions options,
                                      const flutter::DlImageFilter* backdrop) {
  auto paint = options.renders_with_attributes() ? paint_ : Paint{};
  canvas_.SaveLayer(paint, ToRect(bounds), ToImageFilterProc(backdrop));
}

// |flutter::Dispatcher|
void DisplayListDispatcher::restore() {
  canvas_.Restore();
}

// |flutter::Dispatcher|
void DisplayListDispatcher::translate(SkScalar tx, SkScalar ty) {
  canvas_.Translate({tx, ty, 0.0});
}

// |flutter::Dispatcher|
void DisplayListDispatcher::scale(SkScalar sx, SkScalar sy) {
  canvas_.Scale({sx, sy, 1.0});
}

// |flutter::Dispatcher|
void DisplayListDispatcher::rotate(SkScalar degrees) {
  canvas_.Rotate(Degrees{degrees});
}

// |flutter::Dispatcher|
void DisplayListDispatcher::skew(SkScalar sx, SkScalar sy) {
  canvas_.Skew(sx, sy);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::transform2DAffine(SkScalar mxx,
                                              SkScalar mxy,
                                              SkScalar mxt,
                                              SkScalar myx,
                                              SkScalar myy,
                                              SkScalar myt) {
  // clang-format off
  transformFullPerspective(
    mxx, mxy,  0, mxt,
    myx, myy,  0, myt,
    0  ,   0,  1,   0,
    0  ,   0,  0,   1
  );
  // clang-format on
}

// |flutter::Dispatcher|
void DisplayListDispatcher::transformFullPerspective(SkScalar mxx,
                                                     SkScalar mxy,
                                                     SkScalar mxz,
                                                     SkScalar mxt,
                                                     SkScalar myx,
                                                     SkScalar myy,
                                                     SkScalar myz,
                                                     SkScalar myt,
                                                     SkScalar mzx,
                                                     SkScalar mzy,
                                                     SkScalar mzz,
                                                     SkScalar mzt,
                                                     SkScalar mwx,
                                                     SkScalar mwy,
                                                     SkScalar mwz,
                                                     SkScalar mwt) {
  // The order of arguments is row-major but Impeller matrices are
  // column-major.
  // clang-format off
  auto xformation = Matrix{
    mxx, myx, mzx, mwx,
    mxy, myy, mzy, mwy,
    mxz, myz, mzz, mwz,
    mxt, myt, mzt, mwt
  };
  // clang-format on
  canvas_.Transform(xformation);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::transformReset() {
  canvas_.ResetTransform();
}

static Rect ToRect(const SkRect& rect) {
  return Rect::MakeLTRB(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
}

static Entity::ClipOperation ToClipOperation(SkClipOp clip_op) {
  switch (clip_op) {
    case SkClipOp::kDifference:
      return Entity::ClipOperation::kDifference;
    case SkClipOp::kIntersect:
      return Entity::ClipOperation::kIntersect;
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::clipRect(const SkRect& rect,
                                     SkClipOp clip_op,
                                     bool is_aa) {
  auto path = PathBuilder{}.AddRect(ToRect(rect)).TakePath();
  canvas_.ClipPath(path, ToClipOperation(clip_op));
}

static PathBuilder::RoundingRadii ToRoundingRadii(const SkRRect& rrect) {
  using Corner = SkRRect::Corner;
  PathBuilder::RoundingRadii radii;
  radii.bottom_left = ToPoint(rrect.radii(Corner::kLowerLeft_Corner));
  radii.bottom_right = ToPoint(rrect.radii(Corner::kLowerRight_Corner));
  radii.top_left = ToPoint(rrect.radii(Corner::kUpperLeft_Corner));
  radii.top_right = ToPoint(rrect.radii(Corner::kUpperRight_Corner));
  return radii;
}

static Path ToPath(const SkPath& path) {
  auto iterator = SkPath::Iter(path, false);

  struct PathData {
    union {
      SkPoint points[4];
    };
  };

  PathBuilder builder;
  PathData data;
  auto verb = SkPath::Verb::kDone_Verb;
  do {
    verb = iterator.next(data.points);
    switch (verb) {
      case SkPath::kMove_Verb:
        builder.MoveTo(ToPoint(data.points[0]));
        break;
      case SkPath::kLine_Verb:
        builder.LineTo(ToPoint(data.points[1]));
        break;
      case SkPath::kQuad_Verb:
        builder.QuadraticCurveTo(ToPoint(data.points[1]),
                                 ToPoint(data.points[2]));
        break;
      case SkPath::kConic_Verb: {
        constexpr auto kPow2 = 1;  // Only works for sweeps up to 90 degrees.
        constexpr auto kQuadCount = 1 + (2 * (1 << kPow2));
        SkPoint points[kQuadCount];
        const auto curve_count =
            SkPath::ConvertConicToQuads(data.points[0],          //
                                        data.points[1],          //
                                        data.points[2],          //
                                        iterator.conicWeight(),  //
                                        points,                  //
                                        kPow2                    //
            );

        for (int curve_index = 0, point_index = 0;  //
             curve_index < curve_count;             //
             curve_index++, point_index += 2        //
        ) {
          builder.QuadraticCurveTo(ToPoint(points[point_index + 1]),
                                   ToPoint(points[point_index + 2]));
        }
      } break;
      case SkPath::kCubic_Verb:
        builder.CubicCurveTo(ToPoint(data.points[1]), ToPoint(data.points[2]),
                             ToPoint(data.points[3]));
        break;
      case SkPath::kClose_Verb:
        builder.Close();
        break;
      case SkPath::kDone_Verb:
        break;
    }
  } while (verb != SkPath::Verb::kDone_Verb);

  FillType fill_type;
  switch (path.getFillType()) {
    case SkPathFillType::kWinding:
      fill_type = FillType::kNonZero;
      break;
    case SkPathFillType::kEvenOdd:
      fill_type = FillType::kOdd;
      break;
    case SkPathFillType::kInverseWinding:
    case SkPathFillType::kInverseEvenOdd:
      // Flutter doesn't expose these path fill types. These are only visible
      // via the dispatcher interface. We should never get here.
      fill_type = FillType::kNonZero;
      break;
  }
  return builder.TakePath(fill_type);
}

static Path ToPath(const SkRRect& rrect) {
  return PathBuilder{}
      .AddRoundedRect(ToRect(rrect.getBounds()), ToRoundingRadii(rrect))
      .TakePath();
}

// |flutter::Dispatcher|
void DisplayListDispatcher::clipRRect(const SkRRect& rrect,
                                      SkClipOp clip_op,
                                      bool is_aa) {
  canvas_.ClipPath(ToPath(rrect), ToClipOperation(clip_op));
}

// |flutter::Dispatcher|
void DisplayListDispatcher::clipPath(const SkPath& path,
                                     SkClipOp clip_op,
                                     bool is_aa) {
  canvas_.ClipPath(ToPath(path), ToClipOperation(clip_op));
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawColor(flutter::DlColor color,
                                      flutter::DlBlendMode dl_mode) {
  Paint paint;
  paint.color = ToColor(color);
  paint.blend_mode = ToBlendMode(dl_mode);
  canvas_.DrawPaint(paint);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawPaint() {
  canvas_.DrawPaint(paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawLine(const SkPoint& p0, const SkPoint& p1) {
  auto path = PathBuilder{}.AddLine(ToPoint(p0), ToPoint(p1)).TakePath();
  Paint paint = paint_;
  paint.style = Paint::Style::kStroke;
  canvas_.DrawPath(path, paint);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawRect(const SkRect& rect) {
  canvas_.DrawRect(ToRect(rect), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawOval(const SkRect& bounds) {
  auto path = PathBuilder{}.AddOval(ToRect(bounds)).TakePath();
  canvas_.DrawPath(path, paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawCircle(const SkPoint& center, SkScalar radius) {
  auto path = PathBuilder{}.AddCircle(ToPoint(center), radius).TakePath();
  canvas_.DrawPath(path, paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawRRect(const SkRRect& rrect) {
  if (rrect.isSimple()) {
    canvas_.DrawRRect(ToRect(rrect.rect()), rrect.getSimpleRadii().fX, paint_);
  } else {
    canvas_.DrawPath(ToPath(rrect), paint_);
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawDRRect(const SkRRect& outer,
                                       const SkRRect& inner) {
  PathBuilder builder;
  builder.AddPath(ToPath(outer));
  builder.AddPath(ToPath(inner));
  canvas_.DrawPath(builder.TakePath(FillType::kOdd), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawPath(const SkPath& path) {
  canvas_.DrawPath(ToPath(path), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawArc(const SkRect& oval_bounds,
                                    SkScalar start_degrees,
                                    SkScalar sweep_degrees,
                                    bool use_center) {
  PathBuilder builder;
  builder.AddArc(ToRect(oval_bounds), Degrees(start_degrees),
                 Degrees(sweep_degrees), use_center);
  canvas_.DrawPath(builder.TakePath(), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawPoints(SkCanvas::PointMode mode,
                                       uint32_t count,
                                       const SkPoint points[]) {
  Paint paint = paint_;
  paint.style = Paint::Style::kStroke;
  switch (mode) {
    case SkCanvas::kPoints_PointMode:
      if (paint.stroke_cap == Cap::kButt) {
        paint.stroke_cap = Cap::kSquare;
      }
      for (uint32_t i = 0; i < count; i++) {
        Point p0 = ToPoint(points[i]);
        auto path = PathBuilder{}.AddLine(p0, p0).TakePath();
        canvas_.DrawPath(path, paint);
      }
      break;
    case SkCanvas::kLines_PointMode:
      for (uint32_t i = 1; i < count; i += 2) {
        Point p0 = ToPoint(points[i - 1]);
        Point p1 = ToPoint(points[i]);
        auto path = PathBuilder{}.AddLine(p0, p1).TakePath();
        canvas_.DrawPath(path, paint);
      }
      break;
    case SkCanvas::kPolygon_PointMode:
      if (count > 1) {
        Point p0 = ToPoint(points[0]);
        for (uint32_t i = 1; i < count; i++) {
          Point p1 = ToPoint(points[i]);
          auto path = PathBuilder{}.AddLine(p0, p1).TakePath();
          canvas_.DrawPath(path, paint);
          p0 = p1;
        }
      }
      break;
  }
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawSkVertices(const sk_sp<SkVertices> vertices,
                                           SkBlendMode mode) {
  // Needs https://github.com/flutter/flutter/issues/95434
  UNIMPLEMENTED;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawVertices(const flutter::DlVertices* vertices,
                                         flutter::DlBlendMode dl_mode) {
  canvas_.DrawVertices(ToVertices(vertices), ToBlendMode(dl_mode), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawImage(const sk_sp<flutter::DlImage> image,
                                      const SkPoint point,
                                      flutter::DlImageSampling sampling,
                                      bool render_with_attributes) {
  if (!image) {
    return;
  }

  auto texture = image->impeller_texture();
  if (!texture) {
    return;
  }

  const auto size = texture->GetSize();
  const auto src = SkRect::MakeWH(size.width, size.height);
  const auto dest =
      SkRect::MakeXYWH(point.fX, point.fY, size.width, size.height);

  drawImageRect(
      image,                   // image
      src,                     // source rect
      dest,                    // destination rect
      sampling,                // sampling options
      render_with_attributes,  // render with attributes
      SkCanvas::SrcRectConstraint::kStrict_SrcRectConstraint  // constraint
  );
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawImageRect(
    const sk_sp<flutter::DlImage> image,
    const SkRect& src,
    const SkRect& dst,
    flutter::DlImageSampling sampling,
    bool render_with_attributes,
    SkCanvas::SrcRectConstraint constraint) {
  canvas_.DrawImageRect(
      std::make_shared<Image>(image->impeller_texture()),  // image
      ToRect(src),                                         // source rect
      ToRect(dst),                                         // destination rect
      render_with_attributes ? paint_ : Paint(),           // paint
      ToSamplerDescriptor(sampling)                        // sampling
  );
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawImageNine(const sk_sp<flutter::DlImage> image,
                                          const SkIRect& center,
                                          const SkRect& dst,
                                          flutter::DlFilterMode filter,
                                          bool render_with_attributes) {
  NinePatchConverter converter = {};
  converter.DrawNinePatch(
      std::make_shared<Image>(image->impeller_texture()),
      Rect::MakeLTRB(center.fLeft, center.fTop, center.fRight, center.fBottom),
      ToRect(dst), ToSamplerDescriptor(filter), &canvas_, &paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawImageLattice(
    const sk_sp<flutter::DlImage> image,
    const SkCanvas::Lattice& lattice,
    const SkRect& dst,
    flutter::DlFilterMode filter,
    bool render_with_attributes) {
  // Don't implement this one since it is not exposed by flutter,
  // Skia internally converts calls to drawImageNine into this method,
  // which is then converted back to drawImageNine by display list.
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawAtlas(const sk_sp<flutter::DlImage> atlas,
                                      const SkRSXform xform[],
                                      const SkRect tex[],
                                      const flutter::DlColor colors[],
                                      int count,
                                      flutter::DlBlendMode mode,
                                      flutter::DlImageSampling sampling,
                                      const SkRect* cull_rect,
                                      bool render_with_attributes) {
  canvas_.DrawAtlas(std::make_shared<Image>(atlas->impeller_texture()),
                    ToRSXForms(xform, count), ToRects(tex, count),
                    ToColors(colors, count), ToBlendMode(mode),
                    ToSamplerDescriptor(sampling), ToRect(cull_rect), paint_);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawPicture(const sk_sp<SkPicture> picture,
                                        const SkMatrix* matrix,
                                        bool render_with_attributes) {
  // Needs https://github.com/flutter/flutter/issues/95434
  UNIMPLEMENTED;
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawDisplayList(
    const sk_sp<flutter::DisplayList> display_list) {
  int saveCount = canvas_.GetSaveCount();
  Paint savePaint = paint_;
  paint_ = Paint();
  display_list->Dispatch(*this);
  paint_ = savePaint;
  canvas_.RestoreToCount(saveCount);
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawTextBlob(const sk_sp<SkTextBlob> blob,
                                         SkScalar x,
                                         SkScalar y) {
  Scalar scale = canvas_.GetCurrentTransformation().GetMaxBasisLength();
  canvas_.DrawTextFrame(TextFrameFromTextBlob(blob, scale),  //
                        impeller::Point{x, y},               //
                        paint_                               //
  );
}

// |flutter::Dispatcher|
void DisplayListDispatcher::drawShadow(const SkPath& path,
                                       const flutter::DlColor color,
                                       const SkScalar elevation,
                                       bool transparent_occluder,
                                       SkScalar dpr) {
  Color spot_color = ToColor(color);
  spot_color.alpha *= 0.25;

  // Compute the spot color -- ported from SkShadowUtils::ComputeTonalColors.
  {
    Scalar max =
        std::max(std::max(spot_color.red, spot_color.green), spot_color.blue);
    Scalar min =
        std::min(std::min(spot_color.red, spot_color.green), spot_color.blue);
    Scalar luminance = (min + max) * 0.5;

    Scalar alpha_adjust =
        (2.6f + (-2.66667f + 1.06667f * spot_color.alpha) * spot_color.alpha) *
        spot_color.alpha;
    Scalar color_alpha =
        (3.544762f + (-4.891428f + 2.3466f * luminance) * luminance) *
        luminance;
    color_alpha = std::clamp(alpha_adjust * color_alpha, 0.0f, 1.0f);

    Scalar greyscale_alpha =
        std::clamp(spot_color.alpha * (1 - 0.4f * luminance), 0.0f, 1.0f);

    Scalar color_scale = color_alpha * (1 - greyscale_alpha);
    Scalar tonal_alpha = color_scale + greyscale_alpha;
    Scalar unpremul_scale = color_scale / tonal_alpha;
    spot_color = Color(unpremul_scale * spot_color.red,
                       unpremul_scale * spot_color.green,
                       unpremul_scale * spot_color.blue, tonal_alpha);
  }

  Vector3 light_position(0, -1, 1);
  Scalar occluder_z = dpr * elevation;

  constexpr Scalar kLightRadius = 800 / 600;  // Light radius / light height

  Paint paint;
  paint.style = Paint::Style::kFill;
  paint.color = spot_color;
  paint.mask_blur_descriptor = Paint::MaskBlurDescriptor{
      .style = FilterContents::BlurStyle::kNormal,
      .sigma = Radius{kLightRadius * occluder_z /
                      canvas_.GetCurrentTransformation().GetScale().y},
  };

  canvas_.Save();
  canvas_.PreConcat(
      Matrix::MakeTranslation(Vector2(0, -occluder_z * light_position.y)));

  SkRect rect;
  SkRRect rrect;
  if (path.isRect(&rect)) {
    canvas_.DrawRect(ToRect(rect), std::move(paint));
  } else if (path.isRRect(&rrect) && rrect.isSimple()) {
    canvas_.DrawRRect(ToRect(rrect.rect()), rrect.getSimpleRadii().fX,
                      std::move(paint));
  } else {
    canvas_.DrawPath(ToPath(path), paint);
  }

  canvas_.Restore();
}

Picture DisplayListDispatcher::EndRecordingAsPicture() {
  TRACE_EVENT0("impeller", "DisplayListDispatcher::EndRecordingAsPicture");
  return canvas_.EndRecordingAsPicture();
}

}  // namespace impeller
