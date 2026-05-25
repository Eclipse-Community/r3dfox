/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use api::{BorderRadius, BoxShadowClipMode, ClipMode, ColorF, ColorU, PrimitiveKeyKind, PropertyBinding};
use api::units::*;
use crate::border::{BorderRadiusAu};
use crate::clip::{ClipItemKey, ClipItemKeyKind, ClipItemEntry, ClipNodeId};
use crate::intern::{Handle as InternHandle, InternDebug, Internable};
use crate::prim_store::{InternablePrimitive, PrimKey, PrimTemplate, PrimTemplateCommonData};
use crate::prim_store::{PrimitiveInstanceKind, PrimitiveStore, RectangleKey};
use crate::scene_building::{SceneBuilder, IsVisible};
use crate::spatial_tree::SpatialNodeIndex;
use crate::gpu_types::BoxShadowStretchMode;
use crate::render_task_graph::RenderTaskId;
use crate::internal_types::LayoutPrimitiveInfo;

pub type BoxShadowKey = PrimKey<BoxShadow>;

impl BoxShadowKey {
    pub fn new(
        info: &LayoutPrimitiveInfo,
        shadow: BoxShadow,
    ) -> Self {
        BoxShadowKey {
            common: info.into(),
            kind: shadow,
        }
    }
}

impl InternDebug for BoxShadowKey {}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Clone, MallocSizeOf, Hash, Eq, PartialEq)]
pub struct BoxShadow {
    pub color: ColorU,
    pub blur_radius: Au,
    pub clip_mode: BoxShadowClipMode,
    pub inner_shadow_rect: RectangleKey,
    pub outer_shadow_rect: RectangleKey,
    pub shadow_radius: BorderRadiusAu,
    /// The element rect (prim_info.rect) in local space. Used to clip the
    /// element analytically in the shader (clip-out for outset, clip-in for inset).
    pub element_rect: RectangleKey,
    pub element_radius: BorderRadiusAu,
}

impl IsVisible for BoxShadow {
    fn is_visible(&self) -> bool {
        true
    }
}

pub type BoxShadowDataHandle = InternHandle<BoxShadow>;

impl InternablePrimitive for BoxShadow {
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> BoxShadowKey {
        BoxShadowKey::new(info, self)
    }

    fn make_instance_kind(
        _key: BoxShadowKey,
        data_handle: BoxShadowDataHandle,
        _prim_store: &mut PrimitiveStore,
    ) -> PrimitiveInstanceKind {
        PrimitiveInstanceKind::BoxShadow {
            data_handle,
            render_task: None,
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, MallocSizeOf)]
pub struct BoxShadowData {
    pub color: ColorF,
    pub blur_radius: f32,
    pub clip_mode: BoxShadowClipMode,
    pub inner_shadow_rect: LayoutRect,
    pub outer_shadow_rect: LayoutRect,
    pub shadow_radius: BorderRadius,
    pub element_rect: LayoutRect,
    pub element_radius: BorderRadius,
}

impl From<BoxShadow> for BoxShadowData {
    fn from(shadow: BoxShadow) -> Self {
        BoxShadowData {
            color: shadow.color.into(),
            blur_radius: shadow.blur_radius.to_f32_px(),
            clip_mode: shadow.clip_mode,
            inner_shadow_rect: shadow.inner_shadow_rect.into(),
            outer_shadow_rect: shadow.outer_shadow_rect.into(),
            shadow_radius: shadow.shadow_radius.into(),
            element_rect: shadow.element_rect.into(),
            element_radius: shadow.element_radius.into(),
        }
    }
}

pub type BoxShadowTemplate = PrimTemplate<BoxShadowData>;

impl Internable for BoxShadow {
    type Key = BoxShadowKey;
    type StoreData = BoxShadowTemplate;
    type InternData = ();
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_BOX_SHADOWS;
}

impl From<BoxShadowKey> for BoxShadowTemplate {
    fn from(shadow: BoxShadowKey) -> Self {
        BoxShadowTemplate {
            common: PrimTemplateCommonData::with_key_common(shadow.common),
            kind: shadow.kind.into(),
        }
    }
}

#[derive(Debug, Clone, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BoxShadowClipSource {
    // Parameters that define the shadow and are constant.
    pub shadow_radius: BorderRadius,
    pub blur_radius: f32,
    pub clip_mode: BoxShadowClipMode,
    pub stretch_mode_x: BoxShadowStretchMode,
    pub stretch_mode_y: BoxShadowStretchMode,

    // The current cache key (in device-pixels), and handles
    // to the cached clip region and blurred texture.
    pub cache_key: Option<(DeviceIntSize, BoxShadowCacheKey)>,
    pub render_task: Option<RenderTaskId>,

    // Local-space size of the required render task size.
    pub shadow_rect_alloc_size: LayoutSize,

    // Local-space size of the required render task size without any downscaling
    // applied. This is needed to stretch the shadow properly.
    pub original_alloc_size: LayoutSize,

    // The minimal shadow rect for the parameters above,
    // used when drawing the shadow rect to be blurred.
    pub minimal_shadow_rect: LayoutRect,

    // Local space rect for the shadow to be drawn or
    // stretched in the shadow primitive.
    pub prim_shadow_rect: LayoutRect,
}

// The blur shader samples BLUR_SAMPLE_SCALE * blur_radius surrounding texels.
pub const BLUR_SAMPLE_SCALE: f32 = 3.0;

// Maximum blur radius for box-shadows (different than blur filters).
// Taken from nsCSSRendering.cpp in Gecko.
pub const MAX_BLUR_RADIUS: f32 = 300.;

// A cache key that uniquely identifies a minimally sized
// and blurred box-shadow rect that can be stored in the
// texture cache and applied to clip-masks.
#[derive(Debug, Clone, Eq, Hash, MallocSizeOf, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BoxShadowCacheKey {
    /// Blur sigma in device pixels at the mask resolution (≤ MAX_BLUR_STD_DEVIATION after Opt B).
    /// Stored as Au for sub-pixel precision; using i32 would round small sigmas to 0.
    pub blur_radius_dp: Au,
    pub clip_mode: BoxShadowClipMode,
    // NOTE(emilio): Only the original allocation size needs to be in the cache
    // key, since the actual size is derived from that.
    pub original_alloc_size: DeviceIntSize,
    pub br_top_left: DeviceIntSize,
    pub br_top_right: DeviceIntSize,
    pub br_bottom_right: DeviceIntSize,
    pub br_bottom_left: DeviceIntSize,
    pub device_pixel_scale: Au,
}

impl<'a> SceneBuilder<'a> {
    pub fn add_box_shadow(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        prim_info: &LayoutPrimitiveInfo,
        box_offset: &LayoutVector2D,
        color: ColorF,
        mut blur_radius: f32,
        spread_radius: f32,
        border_radius: BorderRadius,
        shadow_radius: BorderRadius,
        clip_mode: BoxShadowClipMode,
    ) {
        if color.a == 0.0 {
            return;
        }

        // Inset shadows get smaller as spread radius increases.
        let (spread_amount, prim_clip_mode) = match clip_mode {
            BoxShadowClipMode::Outset => (spread_radius, ClipMode::ClipOut),
            BoxShadowClipMode::Inset => (-spread_radius, ClipMode::Clip),
        };

        // Ensure the blur radius is somewhat sensible.
        blur_radius = f32::min(blur_radius, MAX_BLUR_RADIUS);

        // Apply parameters that affect where the shadow rect
        // exists in the local space of the primitive.
        let shadow_rect = prim_info
            .rect
            .translate(*box_offset)
            .inflate(spread_amount, spread_amount);

        // If blur radius is zero, we can use a fast path with
        // no blur applied.
        if blur_radius == 0.0 {
            // Trivial reject of box-shadows that are not visible.
            if box_offset.x == 0.0 && box_offset.y == 0.0 && spread_amount == 0.0 {
                return;
            }

            let mut clips = Vec::with_capacity(2);
            let (final_prim_rect, clip_radius) = match clip_mode {
                BoxShadowClipMode::Outset => {
                    if shadow_rect.is_empty() {
                        return;
                    }

                    // TODO(gw): Add a fast path for ClipOut + zero border radius!
                    clips.push(ClipItemEntry {
                        key: ClipItemKey {
                            kind: ClipItemKeyKind::rounded_rect(
                                prim_info.rect.size(),
                                border_radius,
                                ClipMode::ClipOut,
                            ),
                        },
                        spatial_node_index,
                        clip_rect_origin: prim_info.rect.min,
                    });

                    (shadow_rect, shadow_radius)
                }
                BoxShadowClipMode::Inset => {
                    if !shadow_rect.is_empty() {
                        clips.push(ClipItemEntry {
                            key: ClipItemKey {
                                kind: ClipItemKeyKind::rounded_rect(
                                    shadow_rect.size(),
                                    shadow_radius,
                                    ClipMode::ClipOut,
                                ),
                            },
                            spatial_node_index,
                            clip_rect_origin: shadow_rect.min,
                        });
                    }

                    (prim_info.rect, border_radius)
                }
            };

            clips.push(ClipItemEntry {
                key: ClipItemKey {
                    kind: ClipItemKeyKind::rounded_rect(
                        final_prim_rect.size(),
                        clip_radius,
                        ClipMode::Clip,
                    ),
                },
                spatial_node_index,
                clip_rect_origin: final_prim_rect.min,
            });

            self.add_primitive(
                spatial_node_index,
                clip_node_id,
                &LayoutPrimitiveInfo::with_clip_rect(final_prim_rect, prim_info.clip_rect),
                clips,
                PrimitiveKeyKind::Rectangle {
                    color: PropertyBinding::Value(color.into()),
                },
            );
        } else if self.config.use_quad_box_shadow {
            // New quad path for box-shadows with a valid blur radius.
            let blur_offset = (BLUR_SAMPLE_SCALE * blur_radius).ceil();

            // Get the local rect of where the shadow will be drawn,
            // expanded to include room for the blurred region.
            let dest_rect = shadow_rect.inflate(blur_offset, blur_offset);

            match clip_mode {
                BoxShadowClipMode::Outset => {
                    // Certain spread-radii make the shadow invalid.
                    if shadow_rect.is_empty() {
                        return;
                    }

                    // Element clip is handled analytically in the shader.
                    self.add_nonshadowable_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &LayoutPrimitiveInfo::with_clip_rect(dest_rect, prim_info.clip_rect),
                        vec![],
                        BoxShadow {
                            color: color.into(),
                            blur_radius: Au::from_f32_px(blur_radius),
                            clip_mode,
                            inner_shadow_rect: shadow_rect.into(),
                            outer_shadow_rect: dest_rect.into(),
                            shadow_radius: shadow_radius.into(),
                            element_rect: prim_info.rect.into(),
                            element_radius: border_radius.into(),
                        },
                    );
                }
                BoxShadowClipMode::Inset => {
                    // If the inner shadow rect contains the prim
                    // rect, no pixels will be shadowed.
                    if border_radius.is_zero() && shadow_rect
                        .inflate(-blur_radius, -blur_radius)
                        .contains_box(&prim_info.rect)
                    {
                        return;
                    }

                    // Element clip is handled analytically in the shader.
                    self.add_nonshadowable_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &prim_info.clone(),
                        vec![],
                        BoxShadow {
                            color: color.into(),
                            blur_radius: Au::from_f32_px(blur_radius),
                            clip_mode,
                            inner_shadow_rect: shadow_rect.into(),
                            outer_shadow_rect: dest_rect.into(),
                            shadow_radius: shadow_radius.into(),
                            element_rect: prim_info.rect.into(),
                            element_radius: border_radius.into(),
                        },
                    );
                }
            }
        } else {
            self.add_box_shadow_legacy(
                spatial_node_index, clip_node_id, prim_info,
                shadow_rect, color, blur_radius, shadow_radius,
                border_radius, clip_mode, prim_clip_mode,
            );
        }
    }

    /// Legacy clip-mask path, used when `use_quad_box_shadow` is not set.
    /// Renders the blur as a RectanglePrim with a BoxShadow clip item.
    #[allow(clippy::too_many_arguments)]
    fn add_box_shadow_legacy(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        prim_info: &LayoutPrimitiveInfo,
        shadow_rect: LayoutRect,
        color: ColorF,
        blur_radius: f32,
        shadow_radius: BorderRadius,
        border_radius: BorderRadius,
        clip_mode: BoxShadowClipMode,
        prim_clip_mode: ClipMode,
    ) {
        let blur_offset = (BLUR_SAMPLE_SCALE * blur_radius).ceil();
        let dest_rect = shadow_rect.inflate(blur_offset, blur_offset);

        let mut extra_clips = vec![ClipItemEntry {
            key: ClipItemKey {
                kind: ClipItemKeyKind::rounded_rect(
                    prim_info.rect.size(),
                    border_radius,
                    prim_clip_mode,
                ),
            },
            spatial_node_index,
            clip_rect_origin: prim_info.rect.min,
        }];

        let shadow_clip_source = ClipItemEntry {
            key: ClipItemKey {
                kind: ClipItemKeyKind::box_shadow(
                    shadow_rect,
                    shadow_radius,
                    dest_rect,
                    blur_radius,
                    clip_mode,
                ),
            },
            spatial_node_index,
            clip_rect_origin: shadow_rect.min,
        };

        let legacy_prim_info = match clip_mode {
            BoxShadowClipMode::Outset => {
                if shadow_rect.is_empty() {
                    return;
                }
                extra_clips.push(shadow_clip_source);
                LayoutPrimitiveInfo::with_clip_rect(dest_rect, prim_info.clip_rect)
            }
            BoxShadowClipMode::Inset => {
                if border_radius.is_zero() && shadow_rect
                    .inflate(-blur_radius, -blur_radius)
                    .contains_box(&prim_info.rect)
                {
                    return;
                }
                if !shadow_rect.is_empty() {
                    extra_clips.push(shadow_clip_source);
                }
                prim_info.clone()
            }
        };

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            &legacy_prim_info,
            extra_clips,
            PrimitiveKeyKind::Rectangle {
                color: PropertyBinding::Value(color.into()),
            },
        );
    }
}
