/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{PremultipliedColorF, PropertyBinding, ColorU, ColorF, Shadow, RasterSpace};
use crate::scene_building::{CreateShadow, IsVisible};
use crate::intern;
use crate::internal_types::LayoutPrimitiveInfo;
use crate::prim_store::{
    PrimKey, InternablePrimitive, PrimitiveStore, PrimitiveKind,
    PrimTemplate, PrimTemplateCommonData, PrimitiveOpacity,
};
use crate::frame_builder::FrameBuildingState;
use crate::scene::SceneProperties;
use crate::renderer::GpuBufferWriterF;
use std::ops;

#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum RectanglePrim {
    /// Clear an existing rect, used for special effects on some platforms.
    Clear,
    Rectangle {
        color: PropertyBinding<ColorU>,
    },
}

pub type RectangleKey = PrimKey<RectanglePrim>;

pub type RectangleDataHandle = intern::Handle<RectanglePrim>;

impl RectangleKey {
    pub fn new(info: &LayoutPrimitiveInfo, kind: RectanglePrim) -> Self {
        RectangleKey { common: info.into(), kind }
    }
}

impl intern::InternDebug for RectangleKey {}

impl intern::Internable for RectanglePrim {
    type Key = RectangleKey;
    type StoreData = RectangleTemplate;
    type InternData = ();
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_PRIMITIVES;
}

impl InternablePrimitive for RectanglePrim {
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> RectangleKey {
        RectangleKey::new(info, self)
    }

    fn make_instance_kind(
        _key: RectangleKey,
        data_handle: RectangleDataHandle,
        _prim_store: &mut PrimitiveStore,
    ) -> PrimitiveKind {
        match _key.kind {
            RectanglePrim::Clear => {
                PrimitiveKind::Clear {
                    data_handle
                }
            }
            RectanglePrim::Rectangle { .. } => {
                PrimitiveKind::Rectangle {
                    data_handle,
                }
            }
        }
    }
}

impl IsVisible for RectanglePrim {
    fn is_visible(&self) -> bool {
        match *self {
            RectanglePrim::Clear => {
                true
            }
            RectanglePrim::Rectangle { ref color, .. } => {
                match *color {
                    PropertyBinding::Value(value) => value.a > 0,
                    PropertyBinding::Binding(..) => true,
                }
            }
        }
    }
}

impl CreateShadow for RectanglePrim {
    fn create_shadow(
        &self,
        shadow: &Shadow,
        _: bool,
        _: RasterSpace,
    ) -> RectanglePrim {
        match *self {
            RectanglePrim::Rectangle { .. } => {
                RectanglePrim::Rectangle {
                    color: PropertyBinding::Value(shadow.color.into()),
                }
            }
            RectanglePrim::Clear => {
                panic!("bug: this prim is not supported in shadow contexts");
            }
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub enum RectangleData {
    Rectangle {
        color: PropertyBinding<ColorF>,
    },
    Clear,
}

impl RectangleData {
    pub fn write_prim_gpu_blocks(
        &self,
        writer: &mut GpuBufferWriterF,
        scene_properties: &SceneProperties,
    ) {
        match *self {
            RectangleData::Clear => {
                // Opaque black with operator dest out
                writer.push_one(PremultipliedColorF::BLACK);
            }
            RectangleData::Rectangle { ref color, .. } => {
                writer.push_one(scene_properties.resolve_color(color).premultiplied())
            }
        }
    }
}

impl From<RectanglePrim> for RectangleData {
    fn from(kind: RectanglePrim) -> Self {
        match kind {
            RectanglePrim::Clear => {
                RectangleData::Clear
            }
            RectanglePrim::Rectangle { color, .. } => {
                RectangleData::Rectangle {
                    color: color.into(),
                }
            }
        }
    }
}

pub type RectangleTemplate = PrimTemplate<RectangleData>;

impl RectangleTemplate {
    pub fn resolve(&self, scene_properties: &SceneProperties) -> ColorF {
        match self.kind {
            RectangleData::Clear => ColorF::BLACK,
            RectangleData::Rectangle { ref color, .. } => {
                scene_properties.resolve_color(color)
            }
        }
    }
}

impl ops::Deref for RectangleTemplate {
    type Target = PrimTemplateCommonData;
    fn deref(&self) -> &Self::Target {
        &self.common
    }
}

impl ops::DerefMut for RectangleTemplate {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.common
    }
}

impl From<RectangleKey> for RectangleTemplate {
    fn from(item: RectangleKey) -> Self {
        RectangleTemplate {
            common: PrimTemplateCommonData::with_key_common(item.common),
            kind: item.kind.into(),
        }
    }
}

impl RectangleTemplate {
    pub fn update(
        &mut self,
        frame_state: &mut FrameBuildingState,
        scene_properties: &SceneProperties,
    ) {
        let mut writer = frame_state.frame_gpu_data.f32.write_blocks(1);
        self.kind.write_prim_gpu_blocks(&mut writer, scene_properties);
        self.common.gpu_buffer_address = writer.finish();
        self.opacity = match self.kind {
            RectangleData::Clear => {
                PrimitiveOpacity::translucent()
            }
            RectangleData::Rectangle { ref color, .. } => {
                PrimitiveOpacity::from_alpha(scene_properties.resolve_color(color).a)
            }
        };
    }
}

#[test]
#[cfg(target_pointer_width = "64")]
fn test_struct_sizes() {
    use std::mem;
    assert_eq!(mem::size_of::<RectanglePrim>(), 16, "RectanglePrim size changed");
    assert_eq!(mem::size_of::<RectangleTemplate>(), 36, "RectangleTemplate size changed");
    assert_eq!(mem::size_of::<RectangleKey>(), 20, "RectangleKey size changed");
}
