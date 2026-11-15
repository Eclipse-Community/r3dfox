/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VP9HeaderParser.h"
#include <algorithm>
#include <string.h>
namespace mozilla {
uint32_t
VP9BitReader::ReadBits(int n)
{
  if (n < 0 || n > 32) { mError = true; return 0; }
  uint32_t result = 0;
  while (n > 0) {
    if (mBitsLeft == 0) {
      if (mBytesRead >= mSize) {
        mError = true;
        return 0;
      }
      mCurrent = mData[mBytesRead++];
      mBitsLeft = 8;
    }
    int take = std::min(n, mBitsLeft);
    result <<= take;
    result |= (mCurrent >> (mBitsLeft - take)) & ((1 << take) - 1);
    mBitsLeft -= take;
    n -= take;
  }
  return result;
}

int32_t
VP9BitReader::ReadSignedBits(int n)
{
  int32_t v    = (int32_t)ReadBits(n);
  int32_t sign = (int32_t)ReadBit();
  return sign ? -v : v;
}

static void
ReadColorConfig(VP9BitReader& br, uint8_t profile, VP9FrameHeader& h)
{
  if (profile >= 2) {
    br.ReadBit();
  }
  h.colorSpace = (uint8_t)br.ReadBits(3);
  if (h.colorSpace != 7) {
    h.colorRange = (uint8_t)br.ReadBit();
    if (profile == 1 || profile == 3) {
      h.subsamplingX = (uint8_t)br.ReadBit();
      h.subsamplingY = (uint8_t)br.ReadBit();
      br.ReadBit();
    } else {
      h.subsamplingX = 1;
      h.subsamplingY = 1;
    }
  } else {
    h.colorRange = 1;
    if (profile == 1 || profile == 3) {
      h.subsamplingX = 0;
      h.subsamplingY = 0;
      br.ReadBit();
    }
  }
}

static uint32_t
ReadFrameSize(VP9BitReader& br)
{
  return br.ReadBits(16) + 1;
}

void
VP9HeaderParser::ParseLoopFilter(VP9BitReader& br, VP9FrameHeader& h)
{
  h.filterLevel    = (uint8_t)br.ReadBits(6);
  h.sharpnessLevel = (uint8_t)br.ReadBits(3);
  h.modeRefLfEnabled = 0;

  uint8_t modeRefDeltaEnabled = (uint8_t)br.ReadBit();
  h.modeRefDeltaUpdate = 0;
  if (modeRefDeltaEnabled) {
    h.modeRefLfEnabled = 1;
    uint8_t modeRefDeltaUpdate = (uint8_t)br.ReadBit();
    h.modeRefDeltaUpdate = modeRefDeltaUpdate;
    if (modeRefDeltaUpdate) {
      for (int i = 0; i < 4; i++) {
        if (br.ReadBit()) {
          mRefDeltas[i] = (int8_t)br.ReadSignedBits(6);
        }
      }
      for (int i = 0; i < 2; i++) {
        if (br.ReadBit()) {
          mModeDeltas[i] = (int8_t)br.ReadSignedBits(6);
        }
      }
    }
  }
  for (int i = 0; i < 4; i++) { h.refDeltas[i]  = mRefDeltas[i]; }
  for (int i = 0; i < 2; i++) { h.modeDeltas[i] = mModeDeltas[i]; }
}

void
VP9HeaderParser::ParseQuantization(VP9BitReader& br, VP9FrameHeader& h)
{
  h.baseQIndex = (uint8_t)br.ReadBits(8);
  h.deltaQYDc  = br.ReadBit() ? (int8_t)br.ReadSignedBits(4) : 0;
  h.deltaQUvDc = br.ReadBit() ? (int8_t)br.ReadSignedBits(4) : 0;
  h.deltaQUvAc = br.ReadBit() ? (int8_t)br.ReadSignedBits(4) : 0;
  h.lossless   = (h.baseQIndex == 0 &&
                  h.deltaQYDc  == 0 &&
                  h.deltaQUvDc == 0 &&
                  h.deltaQUvAc == 0);
}

void
VP9HeaderParser::ParseSegmentation(VP9BitReader& br, VP9FrameHeader& h)
{
  static const int kSegLvlMax    = 4;
  static const int kMaxSegments  = 8;
  static const int kSegFeatureBits[4] = { 8, 6, 2, 0 };


  h.segmentationEnabled = (uint8_t)br.ReadBit();
  if (!h.segmentationEnabled) {
    return;
  }

  h.segmentationUpdateMap = (uint8_t)br.ReadBit();
  if (h.segmentationUpdateMap) {
    for (int i = 0; i < 7; i++) {
      h.segmentationTreeProbs[i] = br.ReadBit()
                                   ? (uint8_t)br.ReadBits(8)
                                   : 255;
    }
    h.segmentationTemporalUpdate = (uint8_t)br.ReadBit();
    memset(h.segmentationPredProbs, 255, sizeof(h.segmentationPredProbs));
    if (h.segmentationTemporalUpdate) {
      for (int i = 0; i < 3; i++) {
        h.segmentationPredProbs[i] = br.ReadBit()
                                     ? (uint8_t)br.ReadBits(8)
                                     : 255;
      }
    }
  }

  uint8_t segUpdate = (uint8_t)br.ReadBit();
  if (!segUpdate) {
    return;
  }

  memset(h.segFeatureEnabled, 0, sizeof(h.segFeatureEnabled));
  memset(h.segFeatureData, 0, sizeof(h.segFeatureData));
  h.segmentationAbsOrDelta = (uint8_t)br.ReadBit();
  for (int i = 0; i < kMaxSegments; i++) {
    for (int j = 0; j < kSegLvlMax; j++) {
      if (br.ReadBit()) {
        h.segFeatureEnabled[i][j] = 1;
        int bits = kSegFeatureBits[j];
        if (bits > 0) {
          int16_t v = (int16_t)br.ReadBits(bits);
          if (j < 2 && br.ReadBit()) {
            v = -v;
          }
          h.segFeatureData[i][j] = v;
        }
      }
    }
  }
}

void
VP9HeaderParser::ParseTileInfo(VP9BitReader& br, VP9FrameHeader& h)
{
  static const uint32_t kMaxTileWidthB64 = 64;
  static const uint32_t kMinTileWidthB64 = 4;

  uint32_t sbCols = (h.frameWidth + 63) >> 6;

  int minLog2TileCols = 0;
  while ((kMaxTileWidthB64 << minLog2TileCols) < sbCols) {
    minLog2TileCols++;
  }

  int maxLog2TileCols = 1;
  while ((sbCols >> maxLog2TileCols) >= kMinTileWidthB64) {
    maxLog2TileCols++;
  }
  maxLog2TileCols--;
  if (maxLog2TileCols < minLog2TileCols) {
    maxLog2TileCols = minLog2TileCols;
  }

  h.log2TileCols = (uint8_t)minLog2TileCols;
  while (h.log2TileCols < (uint8_t)maxLog2TileCols) {
    if (br.ReadBit()) {
      h.log2TileCols++;
    } else {
      break;
    }
  }

  h.log2TileRows = 0;
  if (br.ReadBit()) {
    h.log2TileRows = 1;
    if (br.ReadBit()) {
      h.log2TileRows = 2;
    }
  }
}

bool
VP9HeaderParser::ParseInternal(const uint8_t* aData, uint32_t aSize,
                                VP9FrameHeader& aHeader)
{
  memset(&aHeader, 0, sizeof(aHeader));

  if (!aData || !aSize) {
    return false;
  }

  aHeader.subsamplingX = aHeader.subsamplingY = 1;
  aHeader.colorSpace = mPrevious.colorSpace;
  aHeader.colorRange = mPrevious.colorRange;
  aHeader.segmentationAbsOrDelta = mPrevious.segmentationAbsOrDelta;
  memcpy(aHeader.segFeatureEnabled, mPrevious.segFeatureEnabled, sizeof(aHeader.segFeatureEnabled));
  memcpy(aHeader.segFeatureData, mPrevious.segFeatureData, sizeof(aHeader.segFeatureData));
  memcpy(aHeader.segmentationTreeProbs, mPrevious.segmentationTreeProbs, sizeof(aHeader.segmentationTreeProbs));
  memcpy(aHeader.segmentationPredProbs, mPrevious.segmentationPredProbs, sizeof(aHeader.segmentationPredProbs));
  VP9BitReader br(aData, aSize);

  uint32_t marker = br.ReadBits(2);
  if (marker != 0x2) {
    return false;
  }

  uint8_t profileLowBit  = (uint8_t)br.ReadBit();
  uint8_t profileHighBit = (uint8_t)br.ReadBit();
  uint8_t profile = (profileHighBit << 1) | profileLowBit;
  if (profile == 3) {
    br.ReadBit();
  }
  aHeader.profile = profile;

  if (profile != 0) { return false; }

  uint8_t showExistingFrame = (uint8_t)br.ReadBit();
  if (showExistingFrame) {
    uint8_t mapIdx = (uint8_t)br.ReadBits(3);
    aHeader = mRefFrames[mapIdx];
    aHeader.showExistingFrame = 1;
    aHeader.frameToShowMapIdx = mapIdx;
    aHeader.frameType = 1;
    aHeader.showFrame = 1;
    aHeader.refreshFrameFlags = 0;
    aHeader.isIntra = false;
    aHeader.uncompressedHeaderSizeBytes = (uint32_t)br.BytesConsumed();
    aHeader.compressedHeaderSize = 0;
    return !br.Failed() && aHeader.frameWidth && aHeader.frameHeight;
  }

  aHeader.frameType          = (uint8_t)br.ReadBit();
  aHeader.showFrame          = (uint8_t)br.ReadBit();
  aHeader.errorResilientMode = (uint8_t)br.ReadBit();

  if (aHeader.frameType == 0) {
    uint8_t s0 = (uint8_t)br.ReadBits(8);
    uint8_t s1 = (uint8_t)br.ReadBits(8);
    uint8_t s2 = (uint8_t)br.ReadBits(8);
    if (s0 != 0x49 || s1 != 0x83 || s2 != 0x42) {
      return false;
    }

    ReadColorConfig(br, profile, aHeader);
    if (aHeader.colorSpace == 7 || aHeader.colorSpace == 6) { return false; }

    aHeader.frameWidth  = ReadFrameSize(br);
    aHeader.frameHeight = ReadFrameSize(br);
    if (br.ReadBit()) {
      aHeader.renderWidth  = ReadFrameSize(br);
      aHeader.renderHeight = ReadFrameSize(br);
    } else {
      aHeader.renderWidth  = aHeader.frameWidth;
      aHeader.renderHeight = aHeader.frameHeight;
    }

    aHeader.refreshFrameFlags = 0xFF;
    aHeader.isIntra = true;

  } else {
    aHeader.isIntra = false;

    uint8_t intraOnly = 0;
    if (!aHeader.showFrame) {
      intraOnly = (uint8_t)br.ReadBit();
    }
    aHeader.isIntra = (intraOnly != 0);

    if (!aHeader.errorResilientMode) {
      aHeader.resetFrameContext = (uint8_t)br.ReadBits(2);
    } else {
      aHeader.resetFrameContext = 0;
    }

    if (intraOnly) {
      uint8_t s0 = (uint8_t)br.ReadBits(8);
      uint8_t s1 = (uint8_t)br.ReadBits(8);
      uint8_t s2 = (uint8_t)br.ReadBits(8);
      if (s0 != 0x49 || s1 != 0x83 || s2 != 0x42) {
        return false;
      }
      if (profile > 0) {
        ReadColorConfig(br, profile, aHeader);
      } else {
        aHeader.colorSpace   = 1;
        aHeader.colorRange   = 0;
        aHeader.subsamplingX = 1;
        aHeader.subsamplingY = 1;
      }
      aHeader.refreshFrameFlags = (uint8_t)br.ReadBits(8);
      aHeader.frameWidth  = ReadFrameSize(br);
      aHeader.frameHeight = ReadFrameSize(br);
      if (br.ReadBit()) {
        aHeader.renderWidth  = ReadFrameSize(br);
        aHeader.renderHeight = ReadFrameSize(br);
      } else {
        aHeader.renderWidth  = aHeader.frameWidth;
        aHeader.renderHeight = aHeader.frameHeight;
      }

    } else {
      aHeader.refreshFrameFlags = (uint8_t)br.ReadBits(8);

      for (int i = 0; i < 3; i++) {
        aHeader.refFrameIdx[i]        = (uint8_t)br.ReadBits(3);
        aHeader.refFrameSignBias[i+1] = (uint8_t)br.ReadBit();
      }

      for (int i = 0; i < 3; ++i) {
        if (!mRefFrames[aHeader.refFrameIdx[i]].frameWidth) { return false; }
      }
      bool foundRef = false;
      for (int i = 0; i < 3; i++) {
        if (br.ReadBit()) {
          aHeader.frameWidth = mRefFrames[aHeader.refFrameIdx[i]].frameWidth;
          aHeader.frameHeight = mRefFrames[aHeader.refFrameIdx[i]].frameHeight;
          foundRef = true;
          break;
        }
      }
      if (!foundRef) {
        aHeader.frameWidth  = ReadFrameSize(br);
        aHeader.frameHeight = ReadFrameSize(br);
      }

      if (br.ReadBit()) {
        aHeader.renderWidth  = ReadFrameSize(br);
        aHeader.renderHeight = ReadFrameSize(br);
      } else {
        aHeader.renderWidth  = aHeader.frameWidth;
        aHeader.renderHeight = aHeader.frameHeight;
      }

      aHeader.allowHighPrecisionMv = (uint8_t)br.ReadBit();
      if (br.ReadBit()) {
        aHeader.interpFilter = 4;
      } else {
        static const uint8_t kLiteralToFilter[4] = { 1, 0, 2, 3 };
        aHeader.interpFilter = kLiteralToFilter[br.ReadBits(2)];
      }
    }
  }

  if (!aHeader.errorResilientMode) {
    aHeader.refreshFrameContext       = (uint8_t)br.ReadBit();
    aHeader.frameParallelDecodingMode = (uint8_t)br.ReadBit();
  } else {
    aHeader.refreshFrameContext       = 0;
    aHeader.frameParallelDecodingMode = 1;
  }
  aHeader.frameContextIdx = (uint8_t)br.ReadBits(2);

  aHeader.usePrevFrameMvs = !aHeader.isIntra && !aHeader.errorResilientMode &&
    !mPrevious.isIntra && mPrevious.showFrame && mPrevious.frameWidth == aHeader.frameWidth &&
    mPrevious.frameHeight == aHeader.frameHeight;
  if (aHeader.isIntra || aHeader.errorResilientMode) {
    mRefDeltas[0] = 1; mRefDeltas[1] = 0;
    mRefDeltas[2] = mRefDeltas[3] = -1;
    mModeDeltas[0] = mModeDeltas[1] = 0;
    aHeader.segmentationAbsOrDelta = 0;
    memset(aHeader.segFeatureEnabled, 0, sizeof(aHeader.segFeatureEnabled));
    memset(aHeader.segFeatureData, 0, sizeof(aHeader.segFeatureData));
    aHeader.frameContextIdx = 0;
  }
  ParseLoopFilter(br, aHeader);
  ParseQuantization(br, aHeader);
  ParseSegmentation(br, aHeader);
  ParseTileInfo(br, aHeader);

  aHeader.compressedHeaderSize = br.ReadBits(16);

  aHeader.uncompressedHeaderSizeBytes = (uint32_t)br.BytesConsumed();

  if (aHeader.frameWidth == 0 || aHeader.frameHeight == 0) {
    return false;
  }

  return !br.Failed() && aHeader.compressedHeaderSize != 0 &&
    aHeader.uncompressedHeaderSizeBytes <= aSize &&
    aHeader.compressedHeaderSize < aSize - aHeader.uncompressedHeaderSizeBytes;
}


void VP9HeaderParser::Reset() {
  memset(&mPrevious, 0, sizeof(mPrevious));
  memset(mPrevious.segmentationTreeProbs, 255, sizeof(mPrevious.segmentationTreeProbs));
  memset(mPrevious.segmentationPredProbs, 255, sizeof(mPrevious.segmentationPredProbs));
  memset(mRefFrames, 0, sizeof(mRefFrames));
  mRefDeltas[0] = 1; mRefDeltas[1] = 0; mRefDeltas[2] = mRefDeltas[3] = -1;
  mModeDeltas[0] = mModeDeltas[1] = 0;
}
bool VP9HeaderParser::Parse(const uint8_t* data, uint32_t size, VP9FrameHeader& h) const {
  VP9HeaderParser candidate = *this;
  return candidate.ParseInternal(data, size, h);
}
void VP9HeaderParser::Commit(const VP9FrameHeader& h) {
  if (h.showExistingFrame) { return; }
  mPrevious = h;
  memcpy(mRefDeltas, h.refDeltas, sizeof(mRefDeltas));
  memcpy(mModeDeltas, h.modeDeltas, sizeof(mModeDeltas));
  for (int i = 0; i < 8; ++i) {
    if (h.refreshFrameFlags & (1 << i)) {
      mRefFrames[i] = h;
    }
  }
}
bool VP9SplitSuperframe(const uint8_t* data, uint32_t size,
                       uint32_t (&offsets)[8], uint32_t (&sizes)[8], uint32_t& count) {
  count = 0;
  if (!data || !size) { return false; }
  uint8_t marker = data[size - 1];
  if ((marker & 0xe0) != 0xc0) {
    offsets[0] = 0; sizes[0] = size; count = 1; return true;
  }
  uint32_t frames = (marker & 7) + 1;
  uint32_t magnitude = ((marker >> 3) & 3) + 1;
  uint32_t indexSize = 2 + frames * magnitude;
  if (size < indexSize || data[size - indexSize] != marker) { return false; }
  uint32_t pos = size - indexSize + 1, total = 0;
  for (uint32_t i = 0; i < frames; ++i) {
    uint32_t length = 0;
    for (uint32_t j = 0; j < magnitude; ++j) { length |= uint32_t(data[pos++]) << (j * 8); }
    if (!length || length > size - indexSize - total) { return false; }
    offsets[i] = total; sizes[i] = length; total += length;
  }
  if (total != size - indexSize) { return false; }
  count = frames;
  return true;
}

}
