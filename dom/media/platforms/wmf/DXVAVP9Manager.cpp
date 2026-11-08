/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DXVAVP9Manager.h"
#include "nsPrintfCString.h"
#include <string.h>

// VP9 DXVA2 GUID: {463707F8-A1D0-4585-876D-83AA6D60B89E}
// its the VP9_VLD_Profile0.
static const GUID sVP9DecoderGUID = {
  0x463707f8, 0xa1d0, 0x4585,
  { 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e }
};

namespace mozilla {

DXVAVP9Manager::DXVAVP9Manager()
  : mNumSurfaces(kDefaultSurfaces)
  , mCurrentSurface(kDefaultSurfaces - 1)
  , mWidth(0)
  , mHeight(0)
  , mFrameCount(0)
  , mLock("DXVAVP9Manager")
{
  memset(mRefFrameSurface, 0xFF, sizeof(mRefFrameSurface));
  memset(mRefFrameWidth, 0, sizeof(mRefFrameWidth));
  memset(mRefFrameHeight, 0, sizeof(mRefFrameHeight));
}

DXVAVP9Manager::~DXVAVP9Manager()
{
  Shutdown();
}

/* static */ const GUID&
DXVAVP9Manager::GetVP9DecoderGUID()
{
  return sVP9DecoderGUID;
}

/* static */ bool
DXVAVP9Manager::SupportsVP9(IDirect3DDeviceManager9* aDeviceManager)
{
  if (!aDeviceManager) {
    return false;
  }

  HANDLE handle = nullptr;
  HRESULT hr = aDeviceManager->OpenDeviceHandle(&handle);
  if (FAILED(hr)) {
    return false;
  }

  RefPtr<IDirectXVideoDecoderService> decoderService;
  hr = aDeviceManager->GetVideoService(
    handle, IID_PPV_ARGS(decoderService.StartAssignment()));
  aDeviceManager->CloseDeviceHandle(handle);
  if (FAILED(hr)) {
    return false;
  }

  UINT  count = 0;
  GUID* guids = nullptr;
  hr = decoderService->GetDecoderDeviceGuids(&count, &guids);
  if (FAILED(hr)) {
    return false;
  }

  bool found = false;
  for (UINT i = 0; i < count; i++) {
    if (guids[i] == sVP9DecoderGUID) {
      found = true;
      break;
    }
  }
  CoTaskMemFree(guids);
  return found;
}

bool
DXVAVP9Manager::Init(IUnknown* aDeviceManager, uint32_t aWidth,
                     uint32_t aHeight, nsACString& aFailureReason)
{
  MutexAutoLock lock(mLock);

  if (!aDeviceManager) { aFailureReason.AssignLiteral("Missing D3D9 device manager"); return false; }

  HRESULT hr = aDeviceManager->QueryInterface(
    __uuidof(IDirect3DDeviceManager9),
    reinterpret_cast<void**>(mDeviceManager.StartAssignment()));
  if (FAILED(hr)) {
    aFailureReason.AssignLiteral(
      "DXVAVP9: QueryInterface to IDirect3DDeviceManager9 failed — "
      "D3D11 path is not supported");
    return false;
  }

  HANDLE handle = nullptr;
  hr = mDeviceManager->OpenDeviceHandle(&handle);
  if (FAILED(hr)) {
    aFailureReason = nsPrintfCString(
      "DXVAVP9: OpenDeviceHandle failed %lX", static_cast<unsigned long>(hr));
    return false;
  }

  hr = mDeviceManager->GetVideoService(
    handle, IID_PPV_ARGS(mDecoderService.StartAssignment()));
  mDeviceManager->CloseDeviceHandle(handle);
  if (FAILED(hr)) {
    aFailureReason = nsPrintfCString(
      "DXVAVP9: GetVideoService failed %lX", static_cast<unsigned long>(hr));
    return false;
  }

  if (!SupportsVP9(mDeviceManager)) {
    aFailureReason.AssignLiteral(
      "DXVAVP9: VP9 DXVA2 GUID not exposed by driver — "
      "GPU/driver does not support hardware VP9 decode");
    return false;
  }

  hr = InitDecoder(aWidth, aHeight);
  if (FAILED(hr)) {
    aFailureReason = nsPrintfCString(
      "DXVAVP9: InitDecoder(%ux%u) failed %lX", aWidth, aHeight,
      static_cast<unsigned long>(hr));
    return false;
  }
  mWidth = aWidth;
  mHeight = aHeight;
  mParser.Reset();
  return true;
}

void
DXVAVP9Manager::Flush()
{
  MutexAutoLock lock(mLock);
  mParser.Reset();
  memset(mRefFrameSurface, 0xff, sizeof(mRefFrameSurface));
  memset(mRefFrameWidth, 0, sizeof(mRefFrameWidth));
  memset(mRefFrameHeight, 0, sizeof(mRefFrameHeight));
}

void
DXVAVP9Manager::Shutdown()
{
  MutexAutoLock lock(mLock);
  FreeSurfaces();
  mParser.Reset();
  mDecoderService = nullptr;
  mDeviceManager = nullptr;
  mWidth = mHeight = 0;
  mFrameCount = 0;
}

HRESULT
DXVAVP9Manager::AllocateSurfaces(uint32_t aWidth, uint32_t aHeight,
                                 uint32_t aCount)
{
  FreeSurfaces();

  if (aCount < kMinSurfaces || aCount > kMaxSurfaces) { return E_INVALIDARG; }

  const uint32_t alignedWidth  = (aWidth  + 15) & ~15u;
  const uint32_t alignedHeight = (aHeight + 15) & ~15u;

  IDirect3DSurface9* surfaces[kMaxSurfaces] = {};
  HRESULT hr = mDecoderService->CreateSurface(
    alignedWidth,
    alignedHeight,
    aCount - 1,
    (D3DFORMAT)MAKEFOURCC('N','V','1','2'),
    D3DPOOL_DEFAULT,
    0,
    DXVA2_VideoDecoderRenderTarget,
    surfaces,
    nullptr);
  // Adopt every returned surface before checking the result, including any
  // partial allocation from a driver failure.
  for (uint32_t i = 0; i < aCount; i++) {
    mSurfaces[i] = dont_AddRef(surfaces[i]);
  }
  if (FAILED(hr)) {
    FreeSurfaces();
    return hr;
  }
  for (uint32_t i = 0; i < aCount; i++) {
    if (!mSurfaces[i]) {
      FreeSurfaces();
      return E_FAIL;
    }
  }
  mNumSurfaces = aCount;
  mCurrentSurface = aCount - 1;
  return S_OK;
}

void
DXVAVP9Manager::FreeSurfaces()
{
  mDecoder = nullptr;
  for (uint32_t i = 0; i < kMaxSurfaces; i++) {
    mSurfaces[i] = nullptr;
  }
  memset(mRefFrameSurface, 0xFF, sizeof(mRefFrameSurface));
  memset(mRefFrameWidth, 0, sizeof(mRefFrameWidth));
  memset(mRefFrameHeight, 0, sizeof(mRefFrameHeight));
  mNumSurfaces = kDefaultSurfaces;
  mCurrentSurface = kDefaultSurfaces - 1;
}

HRESULT
DXVAVP9Manager::InitDecoder(uint32_t aWidth, uint32_t aHeight)
{
  if (!aWidth || !aHeight || aWidth > 8192 || aHeight > 8192) { return E_INVALIDARG; }
  UINT formatCount = 0;
  D3DFORMAT* formats = nullptr;
  HRESULT hr = mDecoderService->GetDecoderRenderTargets(sVP9DecoderGUID, &formatCount, &formats);
  if (FAILED(hr)) { return hr; }
  bool nv12 = false;
  for (UINT i = 0; i < formatCount; ++i) {
    nv12 |= formats[i] == (D3DFORMAT)MAKEFOURCC('N','V','1','2');
  }
  CoTaskMemFree(formats);
  if (!nv12) { return E_FAIL; }

  DXVA2_VideoDesc desc = {};
  desc.SampleWidth                = aWidth;
  desc.SampleHeight               = aHeight;
  desc.Format                     = (D3DFORMAT)MAKEFOURCC('N','V','1','2');
  desc.InputSampleFreq.Numerator  = 30;
  desc.InputSampleFreq.Denominator = 1;
  desc.OutputFrameFreq            = desc.InputSampleFreq;

  UINT configCount = 0;
  DXVA2_ConfigPictureDecode* configs = nullptr;
  hr = mDecoderService->GetDecoderConfigurations(
    sVP9DecoderGUID, &desc, nullptr, &configCount, &configs);
  if (FAILED(hr)) {
    return hr;
  }

  if (configCount == 0) {
    CoTaskMemFree(configs);
    return E_FAIL;
  }

  static const GUID noEncrypt = {0x1b81bed0, 0xa0c7, 0x11d3,
    {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
  DXVA2_ConfigPictureDecode chosenConfig = {};
  bool found = false;
  for (UINT i = 0; i < configCount; ++i) {
    if (configs[i].ConfigBitstreamRaw != 1 &&
        configs[i].ConfigBitstreamRaw != 2) {
      continue;
    }
    if (configs[i].guidConfigBitstreamEncryption != noEncrypt &&
        configs[i].guidConfigBitstreamEncryption != GUID_NULL) {
      continue;
    }
    if (configs[i].ConfigMinRenderTargetBuffCount > kMaxSurfaces) {
      continue;
    }
    chosenConfig = configs[i]; found = true; break;
  }
  CoTaskMemFree(configs);
  if (!found) {
    return E_FAIL;
  }

  uint32_t surfaceCount = kDefaultSurfaces;
  if (chosenConfig.ConfigMinRenderTargetBuffCount > surfaceCount) {
    surfaceCount = chosenConfig.ConfigMinRenderTargetBuffCount;
  }
  if (surfaceCount > kMaxSurfaces) { surfaceCount = kMaxSurfaces; }

  hr = AllocateSurfaces(aWidth, aHeight, surfaceCount);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);

  IDirect3DSurface9* surfacePtrs[kMaxSurfaces];
  for (uint32_t i = 0; i < mNumSurfaces; i++) {
    surfacePtrs[i] = mSurfaces[i];
  }

  hr = mDecoderService->CreateVideoDecoder(
    sVP9DecoderGUID,
    &desc,
    &chosenConfig,
    surfacePtrs,
    mNumSurfaces,
    mDecoder.StartAssignment());
  if (FAILED(hr)) {
    return hr;
  }

  return S_OK;
}

uint32_t
DXVAVP9Manager::PickNextSurface() const
{
  for (uint32_t tries = 0; tries < mNumSurfaces; tries++) {
    uint32_t idx = (mCurrentSurface + 1 + tries) % mNumSurfaces;
    // The preceding frame can supply motion vectors even when it is no longer
    // in the reference map. Keep it alive until the current decode completes.
    bool inUse = idx == mCurrentSurface;
    for (int slot = 0; slot < 8; slot++) {
      if (mRefFrameSurface[slot] == idx) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      return idx;
    }
  }
  // Never overwrite a live reference if the pool invariants are violated.
  return kMaxSurfaces;
}

void
DXVAVP9Manager::FillPicParams(const VP9FrameHeader& h,
                               UXP_DXVA_PicParams_VP9& p)
{
  memset(&p, 0, sizeof(p));

  p.CurrPic.Index7Bits    = (UCHAR)mCurrentSurface;
  p.CurrPic.AssociatedFlag = 0;
  p.profile = 0;

  p.frame_type                   = h.frameType;
  p.show_frame                   = h.showFrame;
  p.error_resilient_mode         = h.errorResilientMode;
  p.subsampling_x                = h.subsamplingX;
  p.subsampling_y                = h.subsamplingY;
  p.refresh_frame_context        = h.refreshFrameContext;
  p.frame_parallel_decoding_mode = h.frameParallelDecodingMode;
  p.intra_only                   = (h.frameType != 0 && h.isIntra) ? 1 : 0;
  p.frame_context_idx            = h.frameContextIdx;
  p.reset_frame_context          = h.resetFrameContext;
  p.allow_high_precision_mv      = (h.frameType != 0) ? h.allowHighPrecisionMv : 0;

  p.BitDepthMinus8Luma   = 0;
  p.BitDepthMinus8Chroma = 0;

  p.interp_filter = h.interpFilter;

  p.width  = h.frameWidth;
  p.height = h.frameHeight;

  for (int i = 0; i < 8; ++i) {
    p.ref_frame_map[i].bPicEntry = mRefFrameSurface[i];
    p.ref_frame_coded_width[i]   = mRefFrameWidth[i];
    p.ref_frame_coded_height[i]  = mRefFrameHeight[i];
  }

  for (int i = 0; i < 3; i++) {
    p.frame_refs[i].bPicEntry = 0xFF;
  }
  if (!h.isIntra) {
    for (int i = 0; i < 3; i++) {
      uint8_t slot = h.refFrameIdx[i] & 7;
      uint8_t surfIdx = mRefFrameSurface[slot];
      p.frame_refs[i].bPicEntry    = surfIdx;
      p.ref_frame_sign_bias[i + 1] = h.refFrameSignBias[i + 1];
    }
  }

  p.filter_level           = (CHAR)h.filterLevel;
  p.sharpness_level        = (CHAR)h.sharpnessLevel;
  p.mode_ref_delta_enabled = h.modeRefLfEnabled;
  p.mode_ref_delta_update  = h.modeRefDeltaUpdate;
  p.use_prev_in_find_mv_refs = h.usePrevFrameMvs;
  for (int i = 0; i < 4; i++) { p.ref_deltas[i]  = h.refDeltas[i]; }
  for (int i = 0; i < 2; i++) { p.mode_deltas[i] = h.modeDeltas[i]; }

  p.base_qindex   = (SHORT)h.baseQIndex;
  p.y_dc_delta_q  = h.deltaQYDc;
  p.uv_dc_delta_q = h.deltaQUvDc;
  p.uv_ac_delta_q = h.deltaQUvAc;

  p.stVP9Segments.enabled         = h.segmentationEnabled;
  p.stVP9Segments.update_map      = h.segmentationUpdateMap;
  p.stVP9Segments.temporal_update = h.segmentationTemporalUpdate;
  p.stVP9Segments.abs_delta       = h.segmentationAbsOrDelta;
  for (int i = 0; i < 7; i++) {
    p.stVP9Segments.tree_probs[i] = h.segmentationTreeProbs[i];
  }
  for (int i = 0; i < 3; i++) {
    p.stVP9Segments.pred_probs[i] = h.segmentationPredProbs[i];
  }
  for (int i = 0; i < 8; i++) {
    UCHAR mask = 0;
    for (int j = 0; j < 4; j++) {
      if (h.segFeatureEnabled[i][j]) {
        mask |= (1 << j);
      }
      p.stVP9Segments.feature_data[i][j] = (SHORT)h.segFeatureData[i][j];
    }
    p.stVP9Segments.feature_mask[i] = mask;
  }

  p.log2_tile_cols = (UCHAR)h.log2TileCols;
  p.log2_tile_rows = (UCHAR)h.log2TileRows;

  p.first_partition_size = (USHORT)h.compressedHeaderSize;
  p.uncompressed_header_size_byte_aligned = (USHORT)h.uncompressedHeaderSizeBytes;

  p.StatusReportFeedbackNumber = ++mFrameCount;
  if (p.StatusReportFeedbackNumber == 0) {
    p.StatusReportFeedbackNumber = ++mFrameCount;
  }
}

HRESULT
DXVAVP9Manager::DecodeFrame(const uint8_t* aData, uint32_t aSize,
                             IDirect3DSurface9** aOutSurface,
                             bool* aOutShouldDisplay,
                             VP9FrameHeader* aOutHeader)
{
  NS_ENSURE_TRUE(aOutSurface && aOutShouldDisplay && aOutHeader, E_POINTER);
  *aOutSurface = nullptr;
  *aOutShouldDisplay = false;
  *aOutHeader = {};
  NS_ENSURE_TRUE(aData && aSize > 0, E_INVALIDARG);

  MutexAutoLock lock(mLock);
  NS_ENSURE_TRUE(mDecoder, E_UNEXPECTED);

  VP9FrameHeader header;
  if (!mParser.Parse(aData, aSize, header)) {
    if (header.profile != 0) {
      return MF_E_INVALIDMEDIATYPE;
    }
    return E_FAIL;
  }

  *aOutHeader = header;

  if (header.showExistingFrame) {
    uint8_t slot = header.frameToShowMapIdx;
    uint8_t surfIdx = mRefFrameSurface[slot];
    if (surfIdx >= mNumSurfaces || !mSurfaces[surfIdx]) {
      return E_FAIL;
    }
    *aOutSurface = mSurfaces[surfIdx];
    (*aOutSurface)->AddRef();
    *aOutShouldDisplay = true;
    return S_OK;
  }

  if (header.frameWidth < 16 || header.frameHeight < 16 ||
      header.frameWidth > 8192 || header.frameHeight > 8192) {
    return MF_E_INVALIDMEDIATYPE;
  }
  if (header.frameWidth != mWidth || header.frameHeight != mHeight) {
    // Recreating the decoder discards every reference surface and entropy
    // context. Only a keyframe makes that transition independent of old state.
    if (header.frameType != 0) { return MF_E_INVALIDMEDIATYPE; }
    HRESULT hr = InitDecoder(header.frameWidth, header.frameHeight);
    NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
    mWidth  = header.frameWidth;
    mHeight = header.frameHeight;
  }

  const uint32_t nextSurface = PickNextSurface();
  if (nextSurface >= mNumSurfaces || !mSurfaces[nextSurface]) {
    return E_FAIL;
  }
  mCurrentSurface = nextSurface;
  IDirect3DSurface9* currentSurf = mSurfaces[mCurrentSurface];

  static const uint32_t kBeginFrameRetries = 50;
  HRESULT hr = E_PENDING;
  for (uint32_t attempt = 0; attempt < kBeginFrameRetries; ++attempt) {
    hr = mDecoder->BeginFrame(currentSurf, nullptr);
    if (hr != E_PENDING) {
      break;
    }
    ::Sleep(2);
  }
  if (FAILED(hr)) {
    return hr;
  }

  auto failWith = [this](HRESULT aHR) -> HRESULT {
    mDecoder->EndFrame(nullptr);
    return aHR;
  };

  if (aSize > UINT32_MAX - 127) { return failWith(E_INVALIDARG); }
  UINT bitstreamCopied = (aSize + 127) & ~127u;
  {
    void* buf = nullptr; UINT size = 0;
    hr = mDecoder->GetBuffer(DXVA2_BitStreamDateBufferType, &buf, &size);
    if (FAILED(hr)) { return failWith(hr); }
    if (!buf || size < bitstreamCopied) {
      mDecoder->ReleaseBuffer(DXVA2_BitStreamDateBufferType);
      return failWith(E_FAIL);
    }
    memcpy(buf, aData, aSize);
    memset(static_cast<uint8_t*>(buf) + aSize, 0, bitstreamCopied - aSize);
    hr = mDecoder->ReleaseBuffer(DXVA2_BitStreamDateBufferType);
    if (FAILED(hr)) { return failWith(hr); }
  }

  UXP_DXVA_PicParams_VP9 picParams;
  FillPicParams(header, picParams);

  UXP_DXVA_Slice_VPx_Short sliceInfo = {};
  sliceInfo.BSNALunitDataLocation = 0;
  sliceInfo.SliceBytesInBuffer    = aSize;
  sliceInfo.wBadSliceChopping     = 0;

  {
    void* buf  = nullptr;
    UINT  size = 0;
    hr = mDecoder->GetBuffer(DXVA2_PictureParametersBufferType, &buf, &size);
    if (FAILED(hr)) { return failWith(hr); }
    if (!buf || size < sizeof(picParams)) {
      mDecoder->ReleaseBuffer(DXVA2_PictureParametersBufferType);
      return failWith(E_FAIL);
    }
    memcpy(buf, &picParams, sizeof(picParams));
    hr = mDecoder->ReleaseBuffer(DXVA2_PictureParametersBufferType);
    if (FAILED(hr)) { return failWith(hr); }
  }

  {
    void* buf  = nullptr;
    UINT  size = 0;
    hr = mDecoder->GetBuffer(DXVA2_SliceControlBufferType, &buf, &size);
    if (FAILED(hr)) { return failWith(hr); }
    if (!buf || size < sizeof(sliceInfo)) {
      mDecoder->ReleaseBuffer(DXVA2_SliceControlBufferType);
      return failWith(E_FAIL);
    }
    memcpy(buf, &sliceInfo, sizeof(sliceInfo));
    hr = mDecoder->ReleaseBuffer(DXVA2_SliceControlBufferType);
    if (FAILED(hr)) { return failWith(hr); }
  }

  DXVA2_DecodeBufferDesc bufDescs[3] = {};
  bufDescs[0].CompressedBufferType = DXVA2_PictureParametersBufferType;
  bufDescs[0].DataSize             = (UINT)sizeof(picParams);
  bufDescs[0].DataOffset           = 0;

  bufDescs[1].CompressedBufferType = DXVA2_SliceControlBufferType;
  bufDescs[1].DataSize             = (UINT)sizeof(sliceInfo);
  bufDescs[1].DataOffset           = 0;

  bufDescs[2].CompressedBufferType = DXVA2_BitStreamDateBufferType;
  bufDescs[2].DataSize             = bitstreamCopied;
  bufDescs[2].DataOffset           = 0;

  DXVA2_DecodeExecuteParams execParams = {};
  execParams.NumCompBuffers     = 3;
  execParams.pCompressedBuffers = bufDescs;

  hr = mDecoder->Execute(&execParams);
  if (FAILED(hr)) {
    return failWith(hr);
  }

  hr = mDecoder->EndFrame(nullptr);
  if (FAILED(hr)) {
    return hr;
  }

  for (int i = 0; i < 8; i++) {
    if (header.refreshFrameFlags & (1 << i)) {
      mRefFrameSurface[i] = (uint8_t)mCurrentSurface;
      mRefFrameWidth[i]   = header.frameWidth;
      mRefFrameHeight[i]  = header.frameHeight;
    }
  }

  mParser.Commit(header);

  *aOutSurface = currentSurf;
  (*aOutSurface)->AddRef();
  *aOutShouldDisplay = (header.showFrame != 0);

  return S_OK;
}

} // namespace mozilla
