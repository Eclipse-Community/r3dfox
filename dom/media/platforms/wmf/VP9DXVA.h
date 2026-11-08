/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VP9DXVA_h_
#define VP9DXVA_h_

#include "WMF.h"
#include <stddef.h>

// Keep private names so older SDKs can use VP9 DXVA without conflicting with
// the native VP9 structures provided by newer Windows SDKs.
#pragma pack(push, 1)

typedef struct _UXP_DXVA_PicEntry_VPx {
  union {
    struct {
      UCHAR Index7Bits    : 7;
      UCHAR AssociatedFlag : 1;
    };
    UCHAR bPicEntry;
  };
} UXP_DXVA_PicEntry_VPx;

typedef struct _UXP_DXVA_segmentation_VP9 {
  union {
    struct {
      UCHAR enabled                    : 1;
      UCHAR update_map                 : 1;
      UCHAR temporal_update            : 1;
      UCHAR abs_delta                  : 1;
      UCHAR ReservedSegmentFlags4Bits  : 4;
    };
    UCHAR wSegmentInfoFlags;
  };
  UCHAR tree_probs[7];
  UCHAR pred_probs[3];
  SHORT feature_data[8][4];
  UCHAR feature_mask[8];
} UXP_DXVA_segmentation_VP9;

typedef struct _UXP_DXVA_PicParams_VP9 {
  UXP_DXVA_PicEntry_VPx CurrPic;
  UCHAR   profile;
  union {
    struct {
      USHORT  frame_type                   : 1;
      USHORT  show_frame                   : 1;
      USHORT  error_resilient_mode         : 1;
      USHORT  subsampling_x                : 1;
      USHORT  subsampling_y                : 1;
      USHORT  extra_plane                  : 1;
      USHORT  refresh_frame_context        : 1;
      USHORT  frame_parallel_decoding_mode : 1;
      USHORT  intra_only                   : 1;
      USHORT  frame_context_idx            : 2;
      USHORT  reset_frame_context          : 2;
      USHORT  allow_high_precision_mv      : 1;
      USHORT  ReservedFormatInfo2Bits      : 2;
    };
    USHORT  wFormatAndPictureInfoFlags;
  };
  UINT    width;
  UINT    height;
  UCHAR   BitDepthMinus8Luma;
  UCHAR   BitDepthMinus8Chroma;
  UCHAR   interp_filter;
  UCHAR   Reserved8Bits;
  UXP_DXVA_PicEntry_VPx ref_frame_map[8];
  UINT    ref_frame_coded_width[8];
  UINT    ref_frame_coded_height[8];
  UXP_DXVA_PicEntry_VPx frame_refs[3];
  CHAR    ref_frame_sign_bias[4];
  CHAR    filter_level;
  CHAR    sharpness_level;
  union {
    struct {
      UCHAR   mode_ref_delta_enabled     : 1;
      UCHAR   mode_ref_delta_update      : 1;
      UCHAR   use_prev_in_find_mv_refs   : 1;
      UCHAR   ReservedControlInfo5Bits   : 5;
    };
    UCHAR   wControlInfoFlags;
  };
  CHAR    ref_deltas[4];
  CHAR    mode_deltas[2];
  SHORT   base_qindex;
  CHAR    y_dc_delta_q;
  CHAR    uv_dc_delta_q;
  CHAR    uv_ac_delta_q;
  UXP_DXVA_segmentation_VP9 stVP9Segments;
  UCHAR   log2_tile_cols;
  UCHAR   log2_tile_rows;
  USHORT  uncompressed_header_size_byte_aligned;
  USHORT  first_partition_size;
  USHORT  Reserved16Bits;
  UINT    Reserved32Bits;
  UINT    StatusReportFeedbackNumber;
} UXP_DXVA_PicParams_VP9;

typedef struct _UXP_DXVA_Slice_VPx_Short {
  UINT    BSNALunitDataLocation;
  UINT    SliceBytesInBuffer;
  USHORT  wBadSliceChopping;
} UXP_DXVA_Slice_VPx_Short;

#pragma pack(pop)


static_assert(sizeof(UXP_DXVA_PicEntry_VPx) == 1, "VP9 picture entry ABI");
static_assert(sizeof(UXP_DXVA_PicParams_VP9) == 208, "VP9 picture ABI");
static_assert(sizeof(UXP_DXVA_segmentation_VP9) == 83, "VP9 segmentation ABI");
static_assert(sizeof(UXP_DXVA_Slice_VPx_Short) == 10, "VP9 slice ABI");
static_assert(offsetof(UXP_DXVA_PicParams_VP9, stVP9Segments) == 109, "VP9 segment offset");
static_assert(offsetof(UXP_DXVA_PicParams_VP9, StatusReportFeedbackNumber) == 204, "VP9 status offset");
#endif
