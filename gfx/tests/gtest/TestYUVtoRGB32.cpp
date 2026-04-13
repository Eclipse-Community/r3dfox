#include "gtest/gtest.h"

#include <array>
#include <cmath>
#include <tuple>
#include <unordered_map>

#include "ImageContainer.h"
#include "YCbCrUtils.h"
#include "nsTArray.h"

using Color = std::tuple<uint8_t, uint8_t, uint8_t>;
using namespace mozilla;

const Color BLACK(0, 0, 0);
const Color BLUE(0, 0, 255);
const Color GREEN(0, 255, 0);
const Color CYAN(0, 255, 255);
const Color RED(255, 0, 0);
const Color MAGENTA(255, 0, 255);
const Color YELLOW(255, 255, 0);
const Color WHITE(255, 255, 255);
const Color CHOCOLATE(210, 105, 30);
const Color PERU(205, 133, 63);
const Color ROSYBROWN(188, 143, 143);
const Color STEELBLUE(70, 130, 180);
const std::array<Color, 12> COLOR_LIST = {
    BLACK,  BLUE,  GREEN,     CYAN, RED,       MAGENTA,
    YELLOW, WHITE, CHOCOLATE, PERU, ROSYBROWN, STEELBLUE};

Color RGB2YUV(const Color& aRGBColor) {
  const uint8_t& r = std::get<0>(aRGBColor);
  const uint8_t& g = std::get<1>(aRGBColor);
  const uint8_t& b = std::get<2>(aRGBColor);

  const double y = r * 0.299 + g * 0.587 + b * 0.114;
  const double u = r * -0.168736 + g * -0.331264 + b * 0.5 + 128;
  const double v = r * 0.5 + g * -0.418688 + b * -0.081312 + 128;

  return Color(round(y), round(u), round(v));
}

int32_t CeilingOfHalf(int32_t aValue) {
  MOZ_ASSERT(aValue >= 0);
  return aValue / 2 + (aValue % 2);
}

already_AddRefed<layers::PlanarYCbCrImage> CreateI420Image(
    const Color& aRGBColor, const gfx::YUVColorSpace& aColorSpace,
    const gfx::IntSize& aSize, Maybe<uint8_t> aAlphaValue = Nothing()) {
  const int32_t halfWidth = CeilingOfHalf(aSize.width);
  const int32_t halfHeight = CeilingOfHalf(aSize.height);

  const size_t yPlaneSize = aSize.width * aSize.height;
  const size_t uPlaneSize = halfWidth * halfHeight;
  const size_t vPlaneSize = uPlaneSize;
  const size_t aPlaneSize = aAlphaValue.isSome() ? yPlaneSize : 0;
  const size_t imageSize = yPlaneSize + uPlaneSize + vPlaneSize + aPlaneSize;

  const Color yuvColor = RGB2YUV(aRGBColor);
  const uint8_t& yColor = std::get<0>(yuvColor);
  const uint8_t& uColor = std::get<1>(yuvColor);
  const uint8_t& vColor = std::get<2>(yuvColor);

  UniquePtr<uint8_t[]> buffer(new uint8_t[imageSize]);

  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect({0, 0}, aSize);

  // Y plane.
  uint8_t* yChannel = buffer.get();
  memset(yChannel, yColor, yPlaneSize);
  data.mYChannel = yChannel;
  data.mYStride = aSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* uChannel = yChannel + yPlaneSize;
  memset(uChannel, uColor, uPlaneSize);
  data.mCbChannel = uChannel;
  data.mCbSkip = 0;

  // Cr plane (aka V).
  uint8_t* vChannel = uChannel + uPlaneSize;
  memset(vChannel, vColor, vPlaneSize);
  data.mCrChannel = vChannel;
  data.mCrSkip = 0;

  // CrCb plane vectors.
  data.mCbCrStride = halfWidth;
  data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;

  // Alpha plane.
  if (aPlaneSize) {
    uint8_t* aChannel = vChannel + vPlaneSize;
    memset(aChannel, *aAlphaValue, aPlaneSize);
    data.mAlpha.emplace();
    data.mAlpha->mChannel = aChannel;
    data.mAlpha->mSize = aSize;
  }

  data.mYUVColorSpace = aColorSpace;

  RefPtr<layers::PlanarYCbCrImage> image =
      new layers::RecyclingPlanarYCbCrImage(new layers::BufferRecycleBin());
  image->CopyData(data);
  return image.forget();
}

already_AddRefed<layers::PlanarYCbCrImage> CreateI444Image(
    const Color& aRGBColor, const gfx::YUVColorSpace& aColorSpace,
    const gfx::IntSize& aSize, Maybe<uint8_t> aAlphaValue = Nothing()) {
  const size_t yPlaneSize = aSize.width * aSize.height;
  const size_t uPlaneSize = yPlaneSize;
  const size_t vPlaneSize = yPlaneSize;
  const size_t aPlaneSize = aAlphaValue.isSome() ? yPlaneSize : 0;
  const size_t imageSize = yPlaneSize + uPlaneSize + vPlaneSize + aPlaneSize;

  const Color yuvColor = RGB2YUV(aRGBColor);
  const uint8_t& yColor = std::get<0>(yuvColor);
  const uint8_t& uColor = std::get<1>(yuvColor);
  const uint8_t& vColor = std::get<2>(yuvColor);

  UniquePtr<uint8_t[]> buffer(new uint8_t[imageSize]);

  layers::PlanarYCbCrData data;
  data.mPictureRect = gfx::IntRect({0, 0}, aSize);

  // Y plane.
  uint8_t* yChannel = buffer.get();
  memset(yChannel, yColor, yPlaneSize);
  data.mYChannel = yChannel;
  data.mYStride = aSize.width;
  data.mYSkip = 0;

  // Cb plane (aka U).
  uint8_t* uChannel = yChannel + yPlaneSize;
  memset(uChannel, uColor, uPlaneSize);
  data.mCbChannel = uChannel;
  data.mCbSkip = 0;

  // Cr plane (aka V).
  uint8_t* vChannel = uChannel + uPlaneSize;
  memset(vChannel, vColor, vPlaneSize);
  data.mCrChannel = vChannel;
  data.mCrSkip = 0;

  // CrCb plane vectors.
  data.mCbCrStride = data.mYStride;
  data.mChromaSubsampling = gfx::ChromaSubsampling::FULL;

  // Alpha plane.
  if (aPlaneSize) {
    uint8_t* aChannel = vChannel + vPlaneSize;
    memset(aChannel, *aAlphaValue, aPlaneSize);
    data.mAlpha.emplace();
    data.mAlpha->mChannel = aChannel;
    data.mAlpha->mSize = aSize;
  }

  data.mYUVColorSpace = aColorSpace;

  RefPtr<layers::PlanarYCbCrImage> image =
      new layers::RecyclingPlanarYCbCrImage(new layers::BufferRecycleBin());
  image->CopyData(data);
  return image.forget();
}

void IsColorEqual(uint8_t* aBGRX, uint8_t* aRGBX, size_t aSize) {
  ASSERT_EQ(aSize % 4, (size_t)0);
  for (size_t i = 0; i < aSize; i += 4) {
    ASSERT_EQ(aBGRX[i + 2], aRGBX[i]);      // R
    ASSERT_EQ(aBGRX[i + 1], aRGBX[i + 1]);  // G
    ASSERT_EQ(aBGRX[i], aRGBX[i + 2]);      // B
    ASSERT_EQ(aBGRX[i + 3], aRGBX[i + 3]);  // X or A
  }
}

uint32_t Hash(const Color& aColor) {
  const uint8_t& r = std::get<0>(aColor);
  const uint8_t& g = std::get<1>(aColor);
  const uint8_t& b = std::get<2>(aColor);
  return r << 16 | g << 8 | b;
}

std::unordered_map<uint32_t, std::array<Color, 3>> GetExpectedConvertedRGB() {
  static std::unordered_map<uint32_t, std::array<Color, 3>> map;
  map.emplace(Hash(BLACK), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(0, 0, 0),
                                                // gfx::YUVColorSpace::BT709
                                                Color(0, 0, 0),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(0, 0, 0)});
  map.emplace(Hash(BLUE), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(0, 82, 0),
                                               // gfx::YUVColorSpace::BT709
                                               Color(0, 54, 0),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(0, 53, 0)});
  map.emplace(Hash(GREEN), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(0, 255, 0),
                                                // gfx::YUVColorSpace::BT709
                                                Color(0, 231, 0),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(0, 242, 0)});
  map.emplace(Hash(CYAN), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(0, 255, 255),
                                               // gfx::YUVColorSpace::BT709
                                               Color(0, 248, 255),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(0, 255, 255)});
  map.emplace(Hash(RED), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                              Color(0, 191, 0),
                                              // gfx::YUVColorSpace::BT709
                                              Color(0, 147, 0),
                                              // gfx::YUVColorSpace::BT2020
                                              Color(0, 162, 0)});
  map.emplace(Hash(MAGENTA), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                  Color(255, 0, 255),
                                                  // gfx::YUVColorSpace::BT709
                                                  Color(255, 28, 255),
                                                  // gfx::YUVColorSpace::BT2020
                                                  Color(255, 18, 255)});
  map.emplace(Hash(YELLOW), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                 Color(255, 255, 0),
                                                 // gfx::YUVColorSpace::BT709
                                                 Color(255, 255, 0),
                                                 // gfx::YUVColorSpace::BT2020
                                                 Color(255, 255, 0)});
  map.emplace(Hash(WHITE), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                                Color(255, 255, 255),
                                                // gfx::YUVColorSpace::BT709
                                                Color(255, 255, 255),
                                                // gfx::YUVColorSpace::BT2020
                                                Color(255, 255, 255)});
  map.emplace(Hash(CHOCOLATE),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(224, 104, 20),
                                   // gfx::YUVColorSpace::BT709
                                   Color(236, 111, 20),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(229, 102, 20)});
  map.emplace(Hash(PERU), std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                               Color(219, 137, 58),
                                               // gfx::YUVColorSpace::BT709
                                               Color(228, 140, 58),
                                               // gfx::YUVColorSpace::BT2020
                                               Color(223, 134, 59)});
  map.emplace(Hash(ROSYBROWN),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(200, 147, 147),
                                   // gfx::YUVColorSpace::BT709
                                   Color(204, 152, 147),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(201, 149, 147)});
  map.emplace(Hash(STEELBLUE),
              std::array<Color, 3>{// gfx::YUVColorSpace::BT601
                                   Color(65, 133, 189),
                                   // gfx::YUVColorSpace::BT709
                                   Color(58, 129, 189),
                                   // gfx::YUVColorSpace::BT2020
                                   Color(62, 135, 189)});
  return map;
}

void IsColorMatched(const Color& aColor, uint8_t* aRGBX, size_t aSize,
                    Maybe<uint8_t> aAlphaValue = Nothing()) {
  const uint8_t& r = std::get<0>(aColor);
  const uint8_t& g = std::get<1>(aColor);
  const uint8_t& b = std::get<2>(aColor);
  for (size_t i = 0; i < aSize; i += 4) {
    ASSERT_EQ(r, aRGBX[i]);      // R
    ASSERT_EQ(g, aRGBX[i + 1]);  // G
    ASSERT_EQ(b, aRGBX[i + 2]);  // B
    if (aAlphaValue) {
      ASSERT_EQ(*aAlphaValue, aRGBX[i + 3]);  // A
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8X8);
  const size_t bufferSize = stride * imgSize.Height();

  const std::array<gfx::YUVColorSpace, 3> colorSpaces{
      gfx::YUVColorSpace::BT601, gfx::YUVColorSpace::BT709,
      gfx::YUVColorSpace::BT2020};

  std::unordered_map<uint32_t, std::array<Color, 3>> expectations =
      GetExpectedConvertedRGB();

  for (const Color& color : COLOR_LIST) {
    const std::array<Color, 3>& expectedColors = expectations[Hash(color)];
    for (const gfx::YUVColorSpace& colorSpace : colorSpaces) {
      RefPtr<layers::PlanarYCbCrImage> img =
          CreateI420Image(color, colorSpace, imgSize);

      UniquePtr<uint8_t[]> BGRX = MakeUnique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8X8,
                          BGRX.get(), stride, nullptr);

      UniquePtr<uint8_t[]> RGBX = MakeUnique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8X8,
                          RGBX.get(), stride, nullptr);

      IsColorEqual(BGRX.get(), RGBX.get(), bufferSize);

      Color expectation = expectedColors[static_cast<size_t>(colorSpace)];
      IsColorMatched(expectation, RGBX.get(), bufferSize);
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32WithAlpha)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8A8);
  const size_t bufferSize = stride * imgSize.Height();

  const std::array<gfx::YUVColorSpace, 3> colorSpaces{
      gfx::YUVColorSpace::BT601, gfx::YUVColorSpace::BT709,
      gfx::YUVColorSpace::BT2020};

  std::unordered_map<uint32_t, std::array<Color, 3>> expectations =
      GetExpectedConvertedRGB();

  for (const Color& color : COLOR_LIST) {
    const std::array<Color, 3>& expectedColors = expectations[Hash(color)];
    for (const gfx::YUVColorSpace& colorSpace : colorSpaces) {
      Maybe<uint8_t> alpha = Some(128);
      RefPtr<layers::PlanarYCbCrImage> img =
          CreateI420Image(color, colorSpace, imgSize, alpha);

      UniquePtr<uint8_t[]> BGRA = MakeUnique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8A8,
                          BGRA.get(), stride, nullptr);

      UniquePtr<uint8_t[]> RGBA = MakeUnique<uint8_t[]>(bufferSize);
      ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8A8,
                          RGBA.get(), stride, nullptr);

      IsColorEqual(BGRA.get(), RGBA.get(), bufferSize);

      Color expectation = expectedColors[static_cast<size_t>(colorSpace)];
      IsColorMatched(expectation, RGBA.get(), bufferSize, alpha);
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32WithIdentityColorSpace)
{
  const gfx::IntSize imgSize(32, 16);
  const int32_t stride =
      imgSize.Width() * gfx::BytesPerPixel(gfx::SurfaceFormat::B8G8R8X8);
  const size_t bufferSize = stride * imgSize.Height();

  for (const Color& color : COLOR_LIST) {
    RefPtr<layers::PlanarYCbCrImage> img =
        CreateI444Image(color, gfx::YUVColorSpace::Identity, imgSize);

    UniquePtr<uint8_t[]> BGRX = MakeUnique<uint8_t[]>(bufferSize);
    ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::B8G8R8X8,
                        BGRX.get(), stride, nullptr);

    UniquePtr<uint8_t[]> RGBX = MakeUnique<uint8_t[]>(bufferSize);
    ConvertYCbCrToRGB32(*img->GetData(), gfx::SurfaceFormat::R8G8B8X8,
                        RGBX.get(), stride, nullptr);

    IsColorEqual(BGRX.get(), RGBX.get(), bufferSize);

    const Color yuvColor = RGB2YUV(color);
    const uint8_t& y = std::get<0>(yuvColor);
    const uint8_t& u = std::get<1>(yuvColor);
    const uint8_t& v = std::get<2>(yuvColor);
    const Color expectation(v, y, u);
    IsColorMatched(expectation, RGBX.get(), bufferSize);
  }
}

// Fills a 4×4 Y plane and chroma planes for a frame whose luma is divided
// into four 2×2 blocks. aColors[blockRow][blockCol] gives the color for each
// block. Chroma plane dimensions depend on aSubsampling:
//   FULL (YV24): 4×4 chroma, each pixel maps 1:1 to a luma pixel.
//   HALF_WIDTH (YV16): 2×4 chroma, half-width but full height.
//   HALF_WIDTH_AND_HEIGHT (YV12): 2×2 chroma.
// Callers must provide at least 16 bytes for aUBuf/aVBuf to cover the FULL
// case; smaller subsamplings use only a prefix of that.
static void FillTwoByTwoFrame(const Color aColors[2][2],
                              gfx::ChromaSubsampling aSubsampling,
                              uint8_t* aYBuf, uint8_t* aUBuf, uint8_t* aVBuf) {
  // Give each luma pixel a unique Y by adding a small per-pixel offset based
  // on its position within its 2x2 chroma block: +0/+2/+4/+6 for
  // (top-left/top-right/bottom-left/bottom-right). This makes luma sampling
  // bugs detectable without meaningfully shifting the color.
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      uint8_t baseY = std::get<0>(RGB2YUV(aColors[r / 2][c / 2]));
      aYBuf[r * 4 + c] = baseY + (r % 2) * 4 + (c % 2) * 2;
    }
  }
  int chromaWidth = (aSubsampling == gfx::ChromaSubsampling::FULL) ? 4 : 2;
  int chromaHeight =
      (aSubsampling == gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT) ? 2 : 4;
  for (int chromaRow = 0; chromaRow < chromaHeight; chromaRow++) {
    int blockRow = chromaRow * 2 / chromaHeight;
    for (int chromaCol = 0; chromaCol < chromaWidth; chromaCol++) {
      int blockCol = chromaCol * 2 / chromaWidth;
      aUBuf[chromaRow * chromaWidth + chromaCol] =
          std::get<1>(RGB2YUV(aColors[blockRow][blockCol]));
      aVBuf[chromaRow * chromaWidth + chromaCol] =
          std::get<2>(RGB2YUV(aColors[blockRow][blockCol]));
    }
  }
}

// Fills and converts a 4×4 test frame, writing the result into aOutput.
// aStride is in bytes. See FillTwoByTwoFrame for the aColors layout.
static void ConvertTestFrame(const Color aColors[2][2],
                             gfx::ChromaSubsampling aSubsampling,
                             const gfx::IntRect& aPictureRect, uint8_t* aOutput,
                             int32_t aStride) {
  uint8_t yBuf[16], uBuf[16], vBuf[16];
  FillTwoByTwoFrame(aColors, aSubsampling, yBuf, uBuf, vBuf);
  layers::PlanarYCbCrData data;
  data.mYChannel = yBuf;
  data.mYStride = 4;
  data.mYSkip = 0;
  data.mCbChannel = uBuf;
  data.mCrChannel = vBuf;
  data.mCbCrStride = (aSubsampling == gfx::ChromaSubsampling::FULL) ? 4 : 2;
  data.mCbSkip = 0;
  data.mCrSkip = 0;
  data.mChromaSubsampling = aSubsampling;
  data.mYUVColorSpace = gfx::YUVColorSpace::BT709;
  data.mColorRange = gfx::ColorRange::LIMITED;
  data.mPictureRect = aPictureRect;
  ConvertYCbCrToRGB32(data, gfx::SurfaceFormat::R8G8B8X8, aOutput, aStride,
                      nullptr);
}

// Tests for odd pic_x / pic_y offsets in YV12, YV16, and YV24.
//
// The 4x4 frame has four 2x2 chroma blocks with distinct mid-range colors.
// Within each block each luma pixel has a unique Y value (offset +0/+2/+4/+6),
// so both chroma and luma misalignment are detectable. The reference is a full
// even-aligned 4x4 conversion; each odd-crop output pixel is checked against
// its corresponding source position in that reference.
static const gfx::ChromaSubsampling kTestSubsamplings[] = {
    gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT,
    gfx::ChromaSubsampling::HALF_WIDTH, gfx::ChromaSubsampling::FULL};

static void RunOddPicTest(const Color aColors[2][2],
                          const gfx::IntRect& aRect) {
  const int32_t stride = aRect.Width() * 4;
  UniquePtr<uint8_t[]> output = MakeUnique<uint8_t[]>(aRect.Height() * stride);
  auto exp = GetExpectedConvertedRGB();
  const size_t bt709 = static_cast<size_t>(gfx::YUVColorSpace::BT709);

  for (gfx::ChromaSubsampling subsampling : kTestSubsamplings) {
    // fullRef: even-aligned 4x4 reference (no odd-offset ambiguity).
    uint8_t fullRef[4 * 4 * 4];
    ConvertTestFrame(aColors, subsampling, gfx::IntRect(0, 0, 4, 4), fullRef,
                     4 * 4);

    // Sanity-check the reference: top-left of each 2x2 block (Y offset 0)
    // must match GetExpectedConvertedRGB; the other three pixels in the block
    // must be distinct but close (Y delta ≤6 → channel delta ~2-7).
    for (int br = 0; br < 2; br++) {
      for (int bc = 0; bc < 2; bc++) {
        uint8_t* base = fullRef + (br * 2) * 4 * 4 + (bc * 2) * 4;
        IsColorMatched(exp[Hash(aColors[br][bc])][bt709], base, 4);
        for (int dr = 0; dr < 2; dr++) {
          for (int dc = 0; dc < 2; dc++) {
            if (dr == 0 && dc == 0) continue;
            uint8_t* other =
                fullRef + (br * 2 + dr) * 4 * 4 + (bc * 2 + dc) * 4;
            for (int ch = 0; ch < 3; ch++) {
              ASSERT_NE(base[ch], other[ch]);
              ASSERT_NEAR(base[ch], other[ch], 10);
            }
          }
        }
      }
    }

    // output: the odd-crop conversion under test.
    ConvertTestFrame(aColors, subsampling, aRect, output.get(), stride);

    // Each output pixel must match its source position in the full reference.
    for (int row = 0; row < aRect.Height(); row++) {
      for (int col = 0; col < aRect.Width(); col++) {
        uint8_t* ref = fullRef + (aRect.y + row) * 4 * 4 + (aRect.x + col) * 4;
        Color expected(ref[0], ref[1], ref[2]);
        IsColorMatched(expected, output.get() + row * stride + col * 4, 4);
      }
    }
  }
}

TEST(YCbCrUtils, ConvertYCbCrToRGB32OddPicOffset)
{
  const Color colors[2][2] = {{CHOCOLATE, PERU}, {ROSYBROWN, STEELBLUE}};
  RunOddPicTest(colors, gfx::IntRect(1, 1, 3, 3));  // both odd
  RunOddPicTest(colors, gfx::IntRect(1, 0, 3, 4));  // odd pic_x only
  RunOddPicTest(colors, gfx::IntRect(0, 1, 4, 3));  // odd pic_y only
}
