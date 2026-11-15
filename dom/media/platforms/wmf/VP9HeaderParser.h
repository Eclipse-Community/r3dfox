/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VP9HeaderParser_h_
#define VP9HeaderParser_h_
#include <stdint.h>
#include <stddef.h>
namespace mozilla {
struct VP9FrameHeader {
  uint8_t  profile;
  // 0 = key frame, 1 = non-key frame
  uint8_t  frameType;
  uint8_t  showFrame;
  uint8_t  showExistingFrame;   // true if this packet just redisplays a ref
  uint8_t  frameToShowMapIdx;   // valid only if showExistingFrame
  uint8_t  errorResilientMode;
  uint8_t  colorSpace;
  uint8_t  colorRange;
  uint8_t  subsamplingX;
  uint8_t  subsamplingY;
  uint32_t frameWidth;
  uint32_t frameHeight;
  uint32_t renderWidth;
  uint32_t renderHeight;
  uint8_t  refreshFrameFlags;
  uint8_t  refFrameIdx[3];
  uint8_t  refFrameSignBias[4];
  uint8_t  allowHighPrecisionMv;
  uint8_t  interpFilter;
  uint8_t  refreshFrameContext;
  uint8_t  frameParallelDecodingMode;
  uint8_t  frameContextIdx;
  uint8_t  resetFrameContext;

  uint8_t  segmentationEnabled;
  uint8_t  segmentationUpdateMap;
  uint8_t  segmentationTemporalUpdate;
  uint8_t  segmentationAbsOrDelta;
  uint8_t  segmentationTreeProbs[7];
  uint8_t  segmentationPredProbs[3];
  uint8_t  segFeatureEnabled[8][4];
  int16_t  segFeatureData[8][4];

  uint8_t  filterLevel;
  uint8_t  sharpnessLevel;
  uint8_t  modeRefLfEnabled;
  uint8_t  modeRefDeltaUpdate;
  int8_t   refDeltas[4];
  int8_t   modeDeltas[2];

  uint8_t  baseQIndex;
  int8_t   deltaQYDc;
  int8_t   deltaQUvDc;
  int8_t   deltaQUvAc;

  uint8_t  log2TileCols;
  uint8_t  log2TileRows;

  uint32_t compressedHeaderSize;

  uint32_t uncompressedHeaderSizeBytes;

  bool     usePrevFrameMvs;
  bool     lossless;
  bool     isIntra;
};

class VP9BitReader {
public:
  VP9BitReader(const uint8_t* aData, size_t aSize)
    : mData(aData), mSize(aSize), mBytesRead(0), mBitsLeft(0), mCurrent(0), mError(false)
  {}

  uint32_t ReadBits(int n);
  uint32_t ReadBit() { return ReadBits(1); }
  uint32_t ReadLiteral(int n) { return ReadBits(n); }
  int32_t  ReadSignedBits(int n);
  bool     Failed() const { return mError; }
  bool     Eof() const { return mBytesRead >= mSize && mBitsLeft == 0; }
  // Loading a byte always consumes at least one bit. This is already the
  // rounded-up byte count and cannot overflow on 32-bit builds.
  size_t BytesConsumed() const { return mBytesRead; }

private:
  const uint8_t* mData;
  size_t         mSize;
  size_t         mBytesRead;
  int            mBitsLeft;
  uint8_t        mCurrent;
  bool           mError;
};

class VP9HeaderParser {
public:
  VP9HeaderParser() { Reset(); }
  void Reset();
  bool Parse(const uint8_t* aData, uint32_t aSize, VP9FrameHeader& aHeader) const;
  void Commit(const VP9FrameHeader& aHeader);
private:
  bool ParseInternal(const uint8_t*, uint32_t, VP9FrameHeader&);
  void ParseLoopFilter(VP9BitReader&, VP9FrameHeader&);
  void ParseQuantization(VP9BitReader&, VP9FrameHeader&);
  void ParseSegmentation(VP9BitReader&, VP9FrameHeader&);
  void ParseTileInfo(VP9BitReader&, VP9FrameHeader&);
  VP9FrameHeader mPrevious;
  // show_existing_frame must restore the geometry and color of the selected
  // reference, which may differ from the most recently decoded frame.
  VP9FrameHeader mRefFrames[8];
  int8_t mRefDeltas[4];
  int8_t mModeDeltas[2];
};

bool VP9SplitSuperframe(const uint8_t* aData, uint32_t aSize,
                       uint32_t (&aOffsets)[8], uint32_t (&aSizes)[8],
                       uint32_t& aCount);

}
#endif
