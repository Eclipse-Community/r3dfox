/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __FilterSupport_h
#define __FilterSupport_h

#include "mozilla/EnumTypeTraits.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "nsRegion.h"
#include "nsTArray.h"

namespace mozilla {
namespace gfx {
class FilterPrimitiveDescription;
class FilterNode;
struct FilterDescription;
}  // namespace gfx
}  // namespace mozilla

extern const float gsRGBToLinearRGBMap[256];

namespace mozilla {
namespace gfx {
namespace FilterWrappers {
extern already_AddRefed<FilterNode> Clear(DrawTarget* aDT);
extern already_AddRefed<FilterNode> ForSurface(
    DrawTarget* aDT, SourceSurface* aSurface, const IntPoint& aSurfacePosition);
}  // namespace FilterWrappers

enum class SVGMorphologyOperator : uint8_t {
  Unknown = 0,
  Erode = 1,
  Dilate = 2,
};

enum class SVGFEColorMatrixType : uint8_t {
  Unknown = 0,
  Matrix = 1,
  Saturate = 2,
  HueRotate = 3,
  LuminanceToAlpha = 4,
  // ColorMatrix types for CSS filters
  Sepia = 5,
};

enum class SVGFEComponentTransferType : uint8_t {
  Unknown = 0,
  Identity = 1,
  Table = 2,
  Discrete = 3,
  Linear = 4,
  Gamma = 5,
  SameAsR = 6,
};

enum class SVGFEBlendMode : uint8_t {
  Unknown = 0,
  Normal = 1,
  Multiply = 2,
  Screen = 3,
  Darken = 4,
  Lighten = 5,
  Overlay = 6,
  ColorDodge = 7,
  ColorBurn = 8,
  HardLight = 9,
  SoftLight = 10,
  Difference = 11,
  Exclusion = 12,
  Hue = 13,
  Saturation = 14,
  Color = 15,
  Luminosity = 16,
};

enum class SVGEdgeMode : uint8_t {
  Unknown = 0,
  Duplicate = 1,
  Wrap = 2,
  None = 3,
};

enum class SVGChannel : uint8_t {
  Unknown = 0,
  R = 1,
  G = 2,
  B = 3,
  A = 4,
};

enum class SVGTurbulenceType : uint8_t {
  Unknown = 0,
  FractalNoise = 1,
  Turbulence = 2,
};

enum class SVGFECompositeOperator : uint8_t {
  Unknown = 0,
  Over = 1,
  In = 2,
  Out = 3,
  Atop = 4,
  Xor = 5,
  Arithmetic = 6,
  Lighter = 7,
};

}  // namespace gfx

template <>
struct MaxContiguousEnumValue<gfx::SVGMorphologyOperator> {
  static constexpr auto value = gfx::SVGMorphologyOperator::Dilate;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGFEColorMatrixType> {
  static constexpr auto value = gfx::SVGFEColorMatrixType::Sepia;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGFEComponentTransferType> {
  static constexpr auto value = gfx::SVGFEComponentTransferType::SameAsR;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGFEBlendMode> {
  static constexpr auto value = gfx::SVGFEBlendMode::Luminosity;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGEdgeMode> {
  static constexpr auto value = gfx::SVGEdgeMode::None;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGChannel> {
  static constexpr auto value = gfx::SVGChannel::A;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGTurbulenceType> {
  static constexpr auto value = gfx::SVGTurbulenceType::Turbulence;
};
template <>
struct MaxContiguousEnumValue<gfx::SVGFECompositeOperator> {
  static constexpr auto value = gfx::SVGFECompositeOperator::Lighter;
};

namespace gfx {

struct FilterAttribute;

// Limits
const float kMaxStdDeviation = 500;

// Simple PrimitiveAttributes:

struct EmptyAttributes {
  bool operator==(const EmptyAttributes& aOther) const { return true; }
};

struct BlendAttributes {
  SVGFEBlendMode mBlendMode;

  bool operator==(const BlendAttributes& aOther) const {
    return mBlendMode == aOther.mBlendMode;
  }
};

struct MorphologyAttributes {
  SVGMorphologyOperator mOperator;
  Size mRadii;

  bool operator==(const MorphologyAttributes& aOther) const {
    return mOperator == aOther.mOperator && mRadii == aOther.mRadii;
  }
};

struct FloodAttributes {
  sRGBColor mColor;

  bool operator==(const FloodAttributes& aOther) const {
    return mColor == aOther.mColor;
  }
};

struct TileAttributes {
  bool operator==(const TileAttributes& aOther) const { return true; }
};

struct OpacityAttributes {
  float mOpacity;

  bool operator==(const OpacityAttributes& aOther) const {
    return mOpacity == aOther.mOpacity;
  }
};

struct OffsetAttributes {
  IntPoint mValue;

  bool operator==(const OffsetAttributes& aOther) const {
    return mValue == aOther.mValue;
  }
};

struct DisplacementMapAttributes {
  float mScale;
  SVGChannel mXChannel;
  SVGChannel mYChannel;

  bool operator==(const DisplacementMapAttributes& aOther) const {
    return mScale == aOther.mScale && mXChannel == aOther.mXChannel &&
           mYChannel == aOther.mYChannel;
  }
};

struct TurbulenceAttributes {
  IntPoint mOffset;
  Size mBaseFrequency;
  float mSeed;
  uint32_t mOctaves;
  bool mStitchable;
  SVGTurbulenceType mType;

  bool operator==(const TurbulenceAttributes& aOther) const {
    return mOffset == aOther.mOffset &&
           mBaseFrequency == aOther.mBaseFrequency && mSeed == aOther.mSeed &&
           mOctaves == aOther.mOctaves && mStitchable == aOther.mStitchable &&
           mType == aOther.mType;
  }
};

struct MergeAttributes {
  bool operator==(const MergeAttributes& aOther) const { return true; }
};

struct ImageAttributes {
  uint32_t mFilter;
  uint32_t mInputIndex;
  Matrix mTransform;

  bool operator==(const ImageAttributes& aOther) const {
    return mFilter == aOther.mFilter && mInputIndex == aOther.mInputIndex &&
           mTransform.ExactlyEquals(aOther.mTransform);
  }
};

struct GaussianBlurAttributes {
  Size mStdDeviation;

  bool operator==(const GaussianBlurAttributes& aOther) const {
    return mStdDeviation == aOther.mStdDeviation;
  }
};

struct DropShadowAttributes {
  Size mStdDeviation;
  Point mOffset;
  sRGBColor mColor;

  bool operator==(const DropShadowAttributes& aOther) const {
    return mStdDeviation == aOther.mStdDeviation && mOffset == aOther.mOffset &&
           mColor == aOther.mColor;
  }
};

struct ToAlphaAttributes {
  bool operator==(const ToAlphaAttributes& aOther) const { return true; }
};

// Complex PrimitiveAttributes:

class ImplicitlyCopyableFloatArray : public CopyableTArray<float> {
 public:
  ImplicitlyCopyableFloatArray() = default;

  ImplicitlyCopyableFloatArray(ImplicitlyCopyableFloatArray&& aOther) = default;

  ImplicitlyCopyableFloatArray& operator=(
      ImplicitlyCopyableFloatArray&& aOther) = default;

  ImplicitlyCopyableFloatArray(const ImplicitlyCopyableFloatArray& aOther) =
      default;

  ImplicitlyCopyableFloatArray& operator=(
      const ImplicitlyCopyableFloatArray& aOther) = default;
};

struct ColorMatrixAttributes {
  SVGFEColorMatrixType mType;
  ImplicitlyCopyableFloatArray mValues;

  bool operator==(const ColorMatrixAttributes& aOther) const {
    return mType == aOther.mType && mValues == aOther.mValues;
  }
};

// If the types for G and B are SVGFEComponentTransferType::SameAsR,
// use the R channel values - this lets us avoid copies.
const uint32_t kChannelROrRGB = 0;
const uint32_t kChannelG = 1;
const uint32_t kChannelB = 2;
const uint32_t kChannelA = 3;

const uint32_t kComponentTransferSlopeIndex = 0;
const uint32_t kComponentTransferInterceptIndex = 1;

const uint32_t kComponentTransferAmplitudeIndex = 0;
const uint32_t kComponentTransferExponentIndex = 1;
const uint32_t kComponentTransferOffsetIndex = 2;

struct ComponentTransferAttributes {
  SVGFEComponentTransferType mTypes[4];
  ImplicitlyCopyableFloatArray mValues[4];

  bool operator==(const ComponentTransferAttributes& aOther) const {
    return mTypes[0] == aOther.mTypes[0] && mTypes[1] == aOther.mTypes[1] &&
           mTypes[2] == aOther.mTypes[2] && mTypes[3] == aOther.mTypes[3] &&
           mValues[0] == aOther.mValues[0] && mValues[1] == aOther.mValues[1] &&
           mValues[2] == aOther.mValues[2] && mValues[3] == aOther.mValues[3];
  }
};

struct ConvolveMatrixAttributes {
  IntSize mKernelSize;
  ImplicitlyCopyableFloatArray mKernelMatrix;
  float mDivisor;
  float mBias;
  IntPoint mTarget;
  SVGEdgeMode mEdgeMode;
  Size mKernelUnitLength;
  bool mPreserveAlpha;

  bool operator==(const ConvolveMatrixAttributes& aOther) const {
    return mKernelSize == aOther.mKernelSize &&
           mKernelMatrix == aOther.mKernelMatrix &&
           mDivisor == aOther.mDivisor && mBias == aOther.mBias &&
           mTarget == aOther.mTarget && mEdgeMode == aOther.mEdgeMode &&
           mKernelUnitLength == aOther.mKernelUnitLength &&
           mPreserveAlpha == aOther.mPreserveAlpha;
  }
};

struct CompositeAttributes {
  SVGFECompositeOperator mOperator;
  ImplicitlyCopyableFloatArray mCoefficients;

  bool operator==(const CompositeAttributes& aOther) const {
    return mOperator == aOther.mOperator &&
           mCoefficients == aOther.mCoefficients;
  }
};

enum class LightType {
  None = 0,
  Point,
  Spot,
  Distant,
  Max,
};

const uint32_t kDistantLightAzimuthIndex = 0;
const uint32_t kDistantLightElevationIndex = 1;
const uint32_t kDistantLightNumAttributes = 2;

const uint32_t kPointLightPositionXIndex = 0;
const uint32_t kPointLightPositionYIndex = 1;
const uint32_t kPointLightPositionZIndex = 2;
const uint32_t kPointLightNumAttributes = 3;

const uint32_t kSpotLightPositionXIndex = 0;
const uint32_t kSpotLightPositionYIndex = 1;
const uint32_t kSpotLightPositionZIndex = 2;
const uint32_t kSpotLightPointsAtXIndex = 3;
const uint32_t kSpotLightPointsAtYIndex = 4;
const uint32_t kSpotLightPointsAtZIndex = 5;
const uint32_t kSpotLightFocusIndex = 6;
const uint32_t kSpotLightLimitingConeAngleIndex = 7;
const uint32_t kSpotLightNumAttributes = 8;

struct DiffuseLightingAttributes {
  LightType mLightType;
  ImplicitlyCopyableFloatArray mLightValues;
  float mSurfaceScale;
  Size mKernelUnitLength;
  sRGBColor mColor;
  float mLightingConstant;
  float mSpecularExponent;

  bool operator==(const DiffuseLightingAttributes& aOther) const {
    return mLightType == aOther.mLightType &&
           mLightValues == aOther.mLightValues &&
           mSurfaceScale == aOther.mSurfaceScale &&
           mKernelUnitLength == aOther.mKernelUnitLength &&
           mColor == aOther.mColor;
  }
};

struct SpecularLightingAttributes : public DiffuseLightingAttributes {};

enum class ColorSpace { SRGB, LinearRGB, Max };

enum class AlphaModel { Unpremultiplied, Premultiplied };

class ColorModel {
 public:
  static ColorModel PremulSRGB() {
    return ColorModel(ColorSpace::SRGB, AlphaModel::Premultiplied);
  }

  ColorModel(ColorSpace aColorSpace, AlphaModel aAlphaModel)
      : mColorSpace(aColorSpace), mAlphaModel(aAlphaModel) {}
  ColorModel()
      : mColorSpace(ColorSpace::SRGB), mAlphaModel(AlphaModel::Premultiplied) {}
  bool operator==(const ColorModel& aOther) const {
    return mColorSpace == aOther.mColorSpace &&
           mAlphaModel == aOther.mAlphaModel;
  }

  // Used to index FilterCachedColorModels::mFilterForColorModel.
  uint8_t ToIndex() const {
    return static_cast<uint8_t>(static_cast<uint8_t>(mColorSpace) << 1) |
           static_cast<uint8_t>(mAlphaModel);
  }

  ColorSpace mColorSpace;
  AlphaModel mAlphaModel;
};

already_AddRefed<FilterNode> FilterNodeGraphFromDescription(
    DrawTarget* aDT, const FilterDescription& aFilter,
    const Rect& aResultNeededRect, FilterNode* aSourceGraphic,
    const IntRect& aSourceGraphicRect, FilterNode* aFillPaint,
    FilterNode* aStrokePaint,
    nsTArray<RefPtr<SourceSurface>>& aAdditionalImages);

/**
 * The methods of this class are not on FilterDescription because
 * FilterDescription is designed as a simple value holder that can be used
 * on any thread.
 */
class FilterSupport {
 public:
  /**
   * Draw the filter described by aFilter. All rect parameters are in filter
   * space coordinates. aRenderRect specifies the part of the filter output
   * that will be drawn at (0, 0) into the draw target aDT, subject to the
   * current transform on aDT but with no additional scaling.
   * The source surfaces must match their corresponding rect in size.
   * aAdditionalImages carries the images that are referenced by the
   * eImageInputIndex attribute on any image primitives in the filter.
   */
  static void RenderFilterDescription(
      DrawTarget* aDT, const FilterDescription& aFilter,
      const Rect& aRenderRect, SourceSurface* aSourceGraphic,
      const IntRect& aSourceGraphicRect, SourceSurface* aFillPaint,
      const IntRect& aFillPaintRect, SourceSurface* aStrokePaint,
      const IntRect& aStrokePaintRect,
      nsTArray<RefPtr<SourceSurface>>& aAdditionalImages,
      const Point& aDestPoint, const DrawOptions& aOptions = DrawOptions());

  /**
   * Computes the region that changes in the filter output due to a change in
   * input.  This is primarily needed when an individual piece of content inside
   * a filtered container element changes.
   */
  static nsIntRegion ComputeResultChangeRegion(
      const FilterDescription& aFilter, const nsIntRegion& aSourceGraphicChange,
      const nsIntRegion& aFillPaintChange,
      const nsIntRegion& aStrokePaintChange);

  /**
   * Computes the regions that need to be supplied in the filter inputs when
   * painting aResultNeededRegion of the filter output.
   */
  static void ComputeSourceNeededRegions(
      const FilterDescription& aFilter, const nsIntRegion& aResultNeededRegion,
      nsIntRegion& aSourceGraphicNeededRegion,
      nsIntRegion& aFillPaintNeededRegion,
      nsIntRegion& aStrokePaintNeededRegion);

  /**
   * Computes the size of the filter output.
   */
  static nsIntRegion ComputePostFilterExtents(
      const FilterDescription& aFilter,
      const nsIntRegion& aSourceGraphicExtents);

  /**
   * Computes the size of a single FilterPrimitiveDescription's output given a
   * set of input extents.
   */
  static nsIntRegion PostFilterExtentsForPrimitive(
      const FilterPrimitiveDescription& aDescription,
      const nsTArray<nsIntRegion>& aInputExtents);
};

/**
 * Create a 4x5 color matrix for the different ways to specify color matrices
 * in SVG.
 *
 * Return false if the input is invalid or if the resulting matrix is the
 * identity.
 */
bool ComputeColorMatrix(const ColorMatrixAttributes& aMatrixAttributes,
                        float aOutMatrix[20]);

}  // namespace gfx
}  // namespace mozilla

#endif  // __FilterSupport_h
