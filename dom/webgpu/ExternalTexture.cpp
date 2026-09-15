/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalTexture.h"

#include "Colorspaces.h"
#include "ImageContainer.h"
#include "mozilla/Assertions.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/VideoBridgeParent.h"
#include "mozilla/webgpu/Queue.h"
#include "mozilla/webgpu/Utility.h"
#include "mozilla/webgpu/WebGPUChild.h"
#include "mozilla/webgpu/WebGPUParent.h"
#include "nsLayoutUtils.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include "mozilla/layers/CompositeProcessD3D11FencesHolderMap.h"
#  include "mozilla/layers/GpuProcessD3D11TextureMap.h"
#  include "mozilla/layers/TextureD3D11.h"
#endif
#ifdef XP_MACOSX
#  include "mozilla/gfx/MacIOSurface.h"
#  include "mozilla/layers/MacIOSurfaceTextureHostOGL.h"
#endif

namespace mozilla::webgpu {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_WEAK_PTR(ExternalTexture, mParent)
GPU_IMPL_JS_WRAP(ExternalTexture)

ExternalTexture::ExternalTexture(Device* const aParent, RawId aId,
                                 RefPtr<ExternalTextureSourceClient> aSource)
    : ObjectBase(aParent->GetChild(), aId,
                 ffi::wgpu_client_drop_external_texture),
      ChildOf(aParent),
      mSource(aSource) {}

ExternalTexture::~ExternalTexture() = default;

/* static */ already_AddRefed<ExternalTexture> ExternalTexture::Create(
    Device* const aParent, const nsString& aLabel,
    const RefPtr<ExternalTextureSourceClient>& aSource,
    dom::PredefinedColorSpace aColorSpace) {
  const webgpu::StringHelper label(aLabel);
  const ffi::WGPUPredefinedColorSpace colorSpace =
      ConvertPredefinedColorSpace(aColorSpace);
  const ffi::WGPUExternalTextureDescriptor desc = {
      .label = label.Get(),
      .source = aSource ? aSource->GetId() : 0,
      .color_space = colorSpace,
  };

  const RawId id = ffi::wgpu_client_create_external_texture(
      aParent->GetClient(), aParent->GetId(), &desc);

  RefPtr<ExternalTexture> externalTexture =
      new ExternalTexture(aParent, id, aSource);
  externalTexture->SetLabel(aLabel);

  return externalTexture.forget();
}

void ExternalTexture::Expire() {
  mIsExpired = true;
  MaybeDestroy();
}

void ExternalTexture::Unexpire() {
  MOZ_ASSERT(!mIsDestroyed);
  MOZ_ASSERT(mSource);
  mIsExpired = false;
}

void ExternalTexture::OnSubmit(uint64_t aSubmissionIndex) {
  mLastSubmittedIndex = aSubmissionIndex;
}

void ExternalTexture::OnSubmittedWorkDone(uint64_t aSubmissionIndex) {
  mLastSubmittedWorkDoneIndex = aSubmissionIndex;
  MaybeDestroy();
}

void ExternalTexture::MaybeDestroy() {
  if (!mIsDestroyed && mIsExpired &&
      mLastSubmittedWorkDoneIndex >= mLastSubmittedIndex) {
    mIsDestroyed = true;
    mSource = nullptr;
    // We could be cleverer and keep the external texture alive until its
    // source is destroyed and there's no chance we could want to reuse the
    // external texture. But that would complicate the logic and perhaps not
    // even gain all that much, as typically attempts to reuse the external
    // texture will occur before the previously submitted work is done, so will
    // be successful anyway.
    ffi::wgpu_client_destroy_external_texture(GetClient(), GetId());
  }
}

RefPtr<ExternalTexture> ExternalTextureCache::GetOrCreate(
    Device* aDevice, const dom::GPUExternalTextureDescriptor& aDesc,
    ErrorResult& aRv) {
  const RefPtr<ExternalTextureSourceClient> source =
      GetOrCreateSource(aDevice, aDesc.mSource, aRv);

  if (source) {
    return source->GetOrCreateExternalTexture(aDevice, aDesc);
  }

  // Create external texture with a null source to indicate error state.
  return ExternalTexture::Create(aDevice, aDesc.mLabel, nullptr,
                                 aDesc.mColorSpace);
}

RefPtr<ExternalTextureSourceClient> ExternalTextureCache::GetOrCreateSource(
    Device* aDevice, const dom::OwningHTMLVideoElementOrVideoFrame& aSource,
    ErrorResult& aRv) {
  RefPtr<layers::Image> image;
  switch (aSource.GetType()) {
    case dom::OwningHTMLVideoElementOrVideoFrame::Type::eHTMLVideoElement:
      image = aSource.GetAsHTMLVideoElement()->GetCurrentImage();
      break;
    case dom::OwningHTMLVideoElementOrVideoFrame::Type::eVideoFrame:
      image = aSource.GetAsVideoFrame()->GetImage();
      break;
  }

  typename decltype(mSources)::AddPtr p;
  if (image) {
    p = mSources.lookupForAdd(image->GetSerial());
    if (p) {
      const RefPtr<ExternalTextureSourceClient> source = p->value();
      MOZ_ASSERT(source->mImage == image);
      return source;
    }
  }

  // If we didn't find an image above we know this is going to fail, but call
  // it anyway so that we can keep all our error handling in one place.
  const RefPtr<ExternalTextureSourceClient> source =
      ExternalTextureSourceClient::Create(aDevice, this, aSource, aRv);
  if (source) {
    // If creating the source succeeded, we must have found an image, which
    // means we must have a valid AddPtr from above.
    // An OOM error in add() just means we don't get to cache the source, but we
    // can still proceed.
    (void)mSources.add(p, source->mImage->GetSerial(), source);
  }
  return source;
}

void ExternalTextureCache::RemoveSource(
    const ExternalTextureSourceClient* aSource) {
  mSources.remove(aSource->mImage->GetSerial());
}

ExternalTextureSourceClient::ExternalTextureSourceClient(
    WebGPUChild* aChild, RawId aId, ExternalTextureCache* aCache,
    const RefPtr<layers::Image>& aImage,
    const std::array<RawId, 3>& aTextureIds,
    const std::array<RawId, 3>& aViewIds)
    : ObjectBase(aChild, aId, ffi::wgpu_client_drop_external_texture_source),
      mImage(aImage),
      mTextureIds(std::move(aTextureIds)),
      mViewIds(std::move(aViewIds)),
      mCache(aCache) {
  MOZ_RELEASE_ASSERT(aId);
}

ExternalTextureSourceClient::~ExternalTextureSourceClient() {
  if (mCache) {
    mCache->RemoveSource(this);
  }

  // Call destroy() in addition to drop() to ensure the plane textures are
  // destroyed immediately. Otherwise they will remain alive until any external
  // textures/bind groups referencing them are garbage collected, which can
  // quickly result in excessive memory usage.
  ffi::wgpu_client_destroy_external_texture_source(GetClient(), GetId());
  // Usually we'd just drop() the textures and views, which would in turn free
  // their IDs. However, we don't know which IDs were used by the host to
  // actually create textures and views with. Therefore the host side is
  // responsible for dropping the textures and views that it actually created,
  // but the client side must free all of the IDs that were made.
  for (const auto id : mViewIds) {
    wgpu_client_free_texture_view_id(GetClient(), id);
  }
  for (const auto id : mTextureIds) {
    wgpu_client_free_texture_id(GetClient(), id);
  }
}

/* static */ already_AddRefed<ExternalTextureSourceClient>
ExternalTextureSourceClient::Create(
    Device* aDevice, ExternalTextureCache* aCache,
    const dom::OwningHTMLVideoElementOrVideoFrame& aSource, ErrorResult& aRv) {
  // Obtain the layers::Image from the HTMLVideoElement or VideoFrame. We use
  // nsLayoutUtils::SurfaceFrom*() instead of directly fetching the image, as it
  // helps with the security checks below. It also helpfully determines the
  // (coded) size, intrinsic size, and crop rect fields for us. Passing
  // SFE_ALLOW_UNCROPPED_UNSCALED ensures it does not create a source surface,
  // as we are able to handle the cropping and scaling ourself.
  const uint32_t flags = nsLayoutUtils::SFE_ALLOW_UNCROPPED_UNSCALED;
  SurfaceFromElementResult sfeResult;
  VideoRotation rotation;
  switch (aSource.GetType()) {
    case dom::OwningHTMLVideoElementOrVideoFrame::Type::eHTMLVideoElement: {
      const auto& videoElement = aSource.GetAsHTMLVideoElement();
      sfeResult = nsLayoutUtils::SurfaceFromElement(videoElement.get(), flags);
      rotation = videoElement->RotationDegrees();
    } break;
    case dom::OwningHTMLVideoElementOrVideoFrame::Type::eVideoFrame: {
      const auto& videoFrame = aSource.GetAsVideoFrame();
      sfeResult = nsLayoutUtils::SurfaceFromVideoFrame(videoFrame.get(), flags);
      rotation = VideoRotation::kDegree_0;
    } break;
  }

  // If source is not origin-clean, throw a SecurityError and return.
  // https://www.w3.org/TR/webgpu/#dom-gpudevice-importexternaltexture
  if (!sfeResult.mCORSUsed) {
    const nsIGlobalObject* const global = aDevice->GetRelevantGlobal();
    nsIPrincipal* const dstPrincipal =
        global ? global->PrincipalOrNull() : nullptr;
    if (!sfeResult.mPrincipal || !dstPrincipal ||
        !dstPrincipal->Subsumes(sfeResult.mPrincipal)) {
      aRv.ThrowSecurityError("Cross-origin elements require CORS!");
      return nullptr;
    }
  }
  if (sfeResult.mIsWriteOnly) {
    aRv.ThrowSecurityError("Write only source data not supported!");
    return nullptr;
  }

  const auto child = aDevice->GetChild();

  // Let usability be ? check the usability of the image argument(source).
  // If usability is not good:
  //   1. Generate a validation error.
  //   2. Return an invalidated GPUExternalTexture.
  // https://www.w3.org/TR/webgpu/#dom-gpudevice-importexternaltexture
  const RefPtr<layers::Image> image = sfeResult.mLayersImage;
  if (!image) {
    ffi::wgpu_report_validation_error(child->GetClient(), aDevice->GetId(),
                                      "Video source's usability is bad");
    return nullptr;
  }

  layers::SurfaceDescriptor sd;
  const nsresult rv = image->BuildSurfaceDescriptorGPUVideoOrBuffer(
      sd, layers::Image::BuildSdbFlags::Default, Nothing(),
      [&](uint32_t aBufferSize) {
        ipc::Shmem buffer;
        if (!child->AllocShmem(aBufferSize, &buffer)) {
          return layers::MemoryOrShmem();
        }
        return layers::MemoryOrShmem(std::move(buffer));
      },
      [&](layers::MemoryOrShmem&& aBuffer) {
        child->DeallocShmem(aBuffer.get_Shmem());
      });
  if (NS_FAILED(rv)) {
    gfxCriticalErrorOnce() << "BuildSurfaceDescriptorGPUVideoOrBuffer failed";
    ffi::wgpu_report_internal_error(
        child->GetClient(), aDevice->GetId(),
        "BuildSurfaceDescriptorGPUVideoOrBuffer failed");
    return nullptr;
  }

  const auto sourceId =
      ffi::wgpu_client_make_external_texture_source_id(child->GetClient());
  // We don't know how many textures or views the host side will need, so make
  // enough IDs for up to 3 of each.
  const std::array<RawId, 3> textureIds{
      ffi::wgpu_client_make_texture_id(child->GetClient()),
      ffi::wgpu_client_make_texture_id(child->GetClient()),
      ffi::wgpu_client_make_texture_id(child->GetClient()),
  };
  const std::array<RawId, 3> viewIds{
      ffi::wgpu_client_make_texture_view_id(child->GetClient()),
      ffi::wgpu_client_make_texture_view_id(child->GetClient()),
      ffi::wgpu_client_make_texture_view_id(child->GetClient()),
  };

  // The actual size of the surface (possibly including non-visible padding).
  // This has not been adjusted for any rotation.
  const gfx::IntSize codedSize = sfeResult.mSize;
  // The crop rectangle to be displayed, defaulting to the full surface if not
  // provided. This is relative to the coded size, and again has not been
  // adjusted for any rotation.
  const gfx::IntRect cropRect =
      sfeResult.mCropRect.valueOr(gfx::IntRect({}, codedSize));
  // The size the surface is intended to be rendered at. We use this for the
  // external texture descriptor's size field which will be the size reported
  // to web content, eg via WGSL's `textureDimensions()` builtin. This has had
  // rotation taken into account.
  const gfx::IntSize intrinsicSize = sfeResult.mIntrinsicSize;

  // Calculate the sample transform, starting with the rotation. As only 90
  // degree increments are supported, we hard-code the values to avoid
  // expensive trig and to keep the numbers precise. If/when we support flips
  // we'd handle that here too.
  gfx::Matrix sampleTransform;
  switch (rotation) {
    case VideoRotation::kDegree_0:
      break;
    case VideoRotation::kDegree_90:
      sampleTransform = gfx::Matrix(0.0, -1.0, 1.0, 0.0, 0.0, 1.0);
      break;
    case VideoRotation::kDegree_180:
      sampleTransform = gfx::Matrix(-1.0, 0.0, 0.0, -1.0, 1.0, 1.0);
      break;
    case VideoRotation::kDegree_270:
      sampleTransform = gfx::Matrix(0.0, 1.0, -1.0, 0.0, 1.0, 0.0);
      break;
  }

  // Scale and translate to account for the crop rect. We need to ensure that
  // the normalized coordinates (0,0)..(1,1) map to the crop rect rather than
  // the coded size. We must therefore normalize the crop rect by dividing by
  // the coded size, then scale and translate the transform based on the
  // normalized crop rect. We apply these transformations pre-rotation as the
  // crop rect itself is expressed pre-rotation. Note the intrinsic size is
  // irrelevant here as we are dealing with normalized coordinates.
  gfx::Rect normalizedCropRect = gfx::Rect(cropRect);
  normalizedCropRect.Scale(1.0 / static_cast<float>(codedSize.width),
                           1.0 / static_cast<float>(codedSize.height));
  sampleTransform.PreTranslate(normalizedCropRect.x, normalizedCropRect.y);
  sampleTransform.PreScale(normalizedCropRect.Width(),
                           normalizedCropRect.Height());

  // Derive the load transform from the sample transform. Texture loads accept
  // unnormalized texel coordinates ranging from (0,0) to the intrinsic size
  // minus one, i.e. based on the size the external texture reports itself as
  // to web content. We need to map these to our rotated crop rect, and the end
  // result must be texel coordinates based on the actual texture size. This
  // can be achieved by first normalizing the coordinates by dividing by the
  // intrinsic size minus one, then applying the sample transformation, then
  // unnormalizing the transformed coordinates by multiplying by the actual
  // texture size minus one.
  gfx::Matrix loadTransform = sampleTransform;
  loadTransform.PreScale(
      1.0 / static_cast<float>(std::max(intrinsicSize.width - 1, 1)),
      1.0 / static_cast<float>(std::max(intrinsicSize.height - 1, 1)));
  loadTransform.PostScale(static_cast<float>(codedSize.width - 1),
                          static_cast<float>(codedSize.height - 1));

  const ExternalTextureSourceDescriptor sourceDesc = {
      .mTextureIds = textureIds,
      .mViewIds = viewIds,
      .mSurfaceDescriptor = std::move(sd),
      .mSize = intrinsicSize,
      .mSampleTransform = {sampleTransform._11, sampleTransform._12,
                           sampleTransform._21, sampleTransform._22,
                           sampleTransform._31, sampleTransform._32},
      .mLoadTransform = {loadTransform._11, loadTransform._12,
                         loadTransform._21, loadTransform._22,
                         loadTransform._31, loadTransform._32},
  };

  // We use a separate IPDL message than Messages() so that IPDL can handle the
  // SurfaceDescriptor (de)serialization for us. We must therefore flush any
  // queued messages first so that they are processed in the correct order.
  child->FlushQueuedMessages();
  child->SendCreateExternalTextureSource(
      aDevice->GetId(), aDevice->GetQueue()->GetId(), sourceId, sourceDesc);

  RefPtr<ExternalTextureSourceClient> source = new ExternalTextureSourceClient(
      child, sourceId, aCache, image, textureIds, viewIds);
  return source.forget();
}

RefPtr<ExternalTexture> ExternalTextureSourceClient::GetOrCreateExternalTexture(
    Device* aDevice, const dom::GPUExternalTextureDescriptor& aDesc) {
  auto p = mExternalTextures.lookupForAdd(aDesc.mColorSpace);
  if (p) {
    if (auto* const externalTexture = p->value().get()) {
      if (!externalTexture->IsDestroyed()) {
        externalTexture->Unexpire();
        return externalTexture;
      }
    }
  }

  const RefPtr<ExternalTexture> externalTexture =
      ExternalTexture::Create(aDevice, aDesc.mLabel, this, aDesc.mColorSpace);

  if (externalTexture) {
    if (p) {
      p->value() = externalTexture;
    } else {
      // OOM error in add() just means we don't get to cache the external
      // texture, but we can still proceed.
      (void)mExternalTextures.add(p, aDesc.mColorSpace, externalTexture);
    }
  }

  return externalTexture;
}

ExternalTextureSourceHost::ExternalTextureSourceHost(
    Span<const RawId> aTextureIds, Span<const RawId> aViewIds,
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
    gfx::YUVRangedColorSpace aColorSpace,
    const std::array<float, 6>& aSampleTransform,
    const std::array<float, 6>& aLoadTransform)
    : mSize(aSize),
      mFormat(aFormat),
      mColorSpace(aColorSpace),
      mSampleTransform(aSampleTransform),
      mLoadTransform(aLoadTransform) {
  mTextureIds.AppendElements(aTextureIds);
  mViewIds.AppendElements(aViewIds);
}

/* static */ ExternalTextureSourceHost ExternalTextureSourceHost::Create(
    WebGPUParent* aParent, RawId aDeviceId, RawId aQueueId,
    const ExternalTextureSourceDescriptor& aDesc) {
  const auto& sd = aDesc.mSurfaceDescriptor;
  switch (sd.type()) {
    case layers::SurfaceDescriptor::TSurfaceDescriptorBuffer: {
      const layers::SurfaceDescriptorBuffer& bufferDesc =
          sd.get_SurfaceDescriptorBuffer();
      ipc::Shmem& bufferShmem = bufferDesc.data().get_Shmem();
      auto source =
          CreateFromBufferDesc(aParent, aDeviceId, aQueueId, aDesc,
                               bufferDesc.desc(), bufferShmem.Range<uint8_t>());
      aParent->DeallocShmem(bufferShmem);
      return source;
    } break;

    case layers::SurfaceDescriptor::TSurfaceDescriptorGPUVideo: {
      const layers::SurfaceDescriptorGPUVideo& gpuVideoDesc =
          sd.get_SurfaceDescriptorGPUVideo();
      const layers::SurfaceDescriptorRemoteDecoder& remoteDecoderDesc =
          gpuVideoDesc.get_SurfaceDescriptorRemoteDecoder();

      const auto videoBridge =
          layers::VideoBridgeParent::GetSingleton(remoteDecoderDesc.source());
      if (!videoBridge) {
        gfxCriticalErrorOnce() << "Failed to get VideoBridge";
        aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                             "Failed to get VideoBridge"_ns);
        return CreateError();
      }
      const RefPtr<layers::TextureHost> textureHost =
          videoBridge->LookupTexture(aParent->mContentId,
                                     remoteDecoderDesc.handle());
      if (!textureHost) {
        gfxCriticalErrorOnce() << "Failed to lookup remote decoder texture";
        aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                             "Failed to lookup remote decoder texture"_ns);
        return CreateError();
      }

      if (const auto* bufferHost = textureHost->AsBufferTextureHost()) {
        return CreateFromBufferDesc(
            aParent, aDeviceId, aQueueId, aDesc,
            bufferHost->GetBufferDescriptor(),
            Span(bufferHost->GetBuffer(), bufferHost->GetBufferSize()));
      } else if (const auto* dxgiHost = textureHost->AsDXGITextureHostD3D11()) {
        return CreateFromDXGITextureHost(aParent, aDeviceId, aQueueId, aDesc,
                                         dxgiHost);
      } else if (const auto* dxgiYCbCrHost =
                     textureHost->AsDXGIYCbCrTextureHostD3D11()) {
        return CreateFromDXGIYCbCrTextureHost(aParent, aDeviceId, aQueueId,
                                              aDesc, dxgiYCbCrHost);
      } else if (const auto* ioSurfHost =
                     textureHost->AsMacIOSurfaceTextureHost()) {
        return CreateFromMacIOSurfaceTextureHost(aParent, aDeviceId, aDesc,
                                                 ioSurfHost);
      } else {
        gfxCriticalErrorOnce()
            << "Unexpected SurfaceDescriptorGPUVideo TextureHost type";
        aParent->ReportError(
            aDeviceId, dom::GPUErrorFilter::Internal,
            "Unexpected SurfaceDescriptorGPUVideo TextureHost type"_ns);
        return CreateError();
      }
    } break;
    default:
      gfxCriticalErrorOnce()
          << "Unexpected SurfaceDescriptor type: " << sd.type();
      aParent->ReportError(
          aDeviceId, dom::GPUErrorFilter::Internal,
          nsPrintfCString("Unexpected SurfaceDescriptor type: %d", sd.type()));
      return CreateError();
  }
  return CreateError();
}

/* static */ ExternalTextureSourceHost
ExternalTextureSourceHost::CreateFromBufferDesc(
    WebGPUParent* aParent, RawId aDeviceId, RawId aQueueId,
    const ExternalTextureSourceDescriptor& aDesc,
    const layers::BufferDescriptor& aBufferDesc, Span<uint8_t> aBuffer) {
  const gfx::SurfaceFormat format =
      layers::ImageDataSerializer::FormatFromBufferDescriptor(aBufferDesc);
  // Creates a texture and view for a single plane, and writes the provided data
  // to the texture.
  auto createPlane = [aParent, aDeviceId, aQueueId](
                         RawId texId, RawId viewId,
                         ffi::WGPUTextureFormat format, gfx::IntSize size,
                         Span<uint8_t> buffer, uint32_t stride) {
    const ffi::WGPUTextureDescriptor textureDesc{
        .size =
            ffi::WGPUExtent3d{
                .width = static_cast<uint32_t>(size.width),
                .height = static_cast<uint32_t>(size.height),
                .depth_or_array_layers = 1,
            },
        .mip_level_count = 1,
        .sample_count = 1,
        .dimension = ffi::WGPUTextureDimension_D2,
        .format = format,
        .usage = WGPUTextureUsages_TEXTURE_BINDING | WGPUTextureUsages_COPY_DST,
        .view_formats = {},
    };

    {
      ErrorBuffer error;
      ffi::wgpu_server_device_create_texture(
          aParent->GetContext(), aDeviceId, texId, &textureDesc, error.ToFFI());
      // Since we have full control over the creation of this texture, any
      // validation error we encounter should be treated as an internal error.
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }

    const ffi::WGPUTexelCopyTextureInfo dest{
        .texture = texId,
        .mip_level = 0,
        .origin = {},
        .aspect = ffi::WGPUTextureAspect_All,
    };

    const ffi::WGPUTexelCopyBufferLayout layout{
        .offset = 0,
        .bytes_per_row = &stride,
        .rows_per_image = nullptr,
    };
    const auto len = CheckedInt<size_t>(size.height) * stride;
    MOZ_RELEASE_ASSERT(len.isValid());
    const Span<uint8_t> slice = buffer.to(len.value());
    const ffi::WGPUFfiSlice_u8 data{
        .data = slice.data(),
        .length = slice.size(),
    };
    {
      ErrorBuffer error;
      ffi::wgpu_server_queue_write_texture(aParent->GetContext(), aDeviceId,
                                           aQueueId, &dest, data, &layout,
                                           &textureDesc.size, error.ToFFI());
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }

    const ffi::WGPUTextureViewDescriptor viewDesc{};
    {
      ErrorBuffer error;
      ffi::wgpu_server_texture_create_view(aParent->GetContext(), aDeviceId,
                                           texId, viewId, &viewDesc,
                                           error.ToFFI());
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
  };

  AutoTArray<RawId, 3> usedTextureIds;
  AutoTArray<RawId, 3> usedViewIds;
  gfx::YUVRangedColorSpace colorSpace;
  switch (aBufferDesc.type()) {
    case layers::BufferDescriptor::TRGBDescriptor: {
      const layers::RGBDescriptor& rgbDesc = aBufferDesc.get_RGBDescriptor();
      ffi::WGPUTextureFormat planeFormat;
      switch (rgbDesc.format()) {
        case gfx::SurfaceFormat::B8G8R8A8:
        case gfx::SurfaceFormat::B8G8R8X8:
          planeFormat = {ffi::WGPUTextureFormat_Bgra8Unorm};
          break;
        case gfx::SurfaceFormat::R8G8B8A8:
        case gfx::SurfaceFormat::R8G8B8X8:
          planeFormat = {ffi::WGPUTextureFormat_Rgba8Unorm};
          break;
        default:
          gfxCriticalErrorOnce()
              << "Unexpected RGBDescriptor format: " << rgbDesc.format();
          aParent->ReportError(
              aDeviceId, dom::GPUErrorFilter::Internal,
              nsPrintfCString("Unexpected RGBDescriptor format: %s",
                              mozilla::ToString(rgbDesc.format()).c_str()));
          return CreateError();
      }
      auto stride = layers::ImageDataSerializer::GetRGBStride(rgbDesc);
      if (stride.isNothing()) {
        gfxCriticalErrorOnce() << "Invalid stride";
        aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                             "Invalid stride"_ns);
        return CreateError();
      }
      createPlane(aDesc.mTextureIds[0], aDesc.mViewIds[0], planeFormat,
                  rgbDesc.size(), aBuffer, stride.value());
      usedTextureIds.AppendElement(aDesc.mTextureIds[0]);
      usedViewIds.AppendElement(aDesc.mViewIds[0]);
      // TODO: support HLG and PQ
      // https://bugzilla.mozilla.org/show_bug.cgi?id=2024870
      colorSpace = gfx::YUVRangedColorSpace::GbrIdentity;
    } break;
    case layers::BufferDescriptor::TYCbCrDescriptor: {
      const layers::YCbCrDescriptor& yCbCrDesc =
          aBufferDesc.get_YCbCrDescriptor();
      const gfx::IntSize ySize =
          layers::ImageDataSerializer::SizeFromBufferDescriptor(aBufferDesc);
      const gfx::IntSize cbCrSize =
          layers::ImageDataSerializer::GetCroppedCbCrSize(aBufferDesc);

      ffi::WGPUTextureFormat planeFormat;
      switch (yCbCrDesc.colorDepth()) {
        case gfx::ColorDepth::COLOR_8:
          planeFormat = {ffi::WGPUTextureFormat_R8Unorm};
          break;
        case gfx::ColorDepth::COLOR_10:
        case gfx::ColorDepth::COLOR_12:
        case gfx::ColorDepth::COLOR_16:
          gfxCriticalNoteOnce << "Unsupported color depth: "
                              << yCbCrDesc.colorDepth();
          aParent->ReportError(
              aDeviceId, dom::GPUErrorFilter::Internal,
              nsPrintfCString(
                  "Unsupported color depth: %s",
                  mozilla::ToString(yCbCrDesc.colorDepth()).c_str()));
          return CreateError();
      }

      createPlane(aDesc.mTextureIds[0], aDesc.mViewIds[0], planeFormat, ySize,
                  aBuffer.from(yCbCrDesc.yOffset()), yCbCrDesc.yStride());
      createPlane(aDesc.mTextureIds[1], aDesc.mViewIds[1], planeFormat,
                  cbCrSize, aBuffer.from(yCbCrDesc.cbOffset()),
                  yCbCrDesc.cbCrStride());
      createPlane(aDesc.mTextureIds[2], aDesc.mViewIds[2], planeFormat,
                  cbCrSize, aBuffer.from(yCbCrDesc.crOffset()),
                  yCbCrDesc.cbCrStride());
      usedTextureIds.AppendElements(aDesc.mTextureIds.data(),
                                    aDesc.mTextureIds.size());
      usedViewIds.AppendElements(aDesc.mViewIds.data(), aDesc.mViewIds.size());
      colorSpace = gfx::ToYUVRangedColorSpace(yCbCrDesc.yUVColorSpace(),
                                              yCbCrDesc.colorRange(),
                                              yCbCrDesc.transferFunction());
    } break;
    case layers::BufferDescriptor::T__None: {
      gfxCriticalErrorOnce() << "Invalid BufferDescriptor";
      aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                           "Invalid BufferDescriptor"_ns);
      return CreateError();
    } break;
  }

  return ExternalTextureSourceHost(usedTextureIds, usedViewIds, aDesc.mSize,
                                   format, colorSpace, aDesc.mSampleTransform,
                                   aDesc.mLoadTransform);
}

/* static */ ExternalTextureSourceHost
ExternalTextureSourceHost::CreateError() {
  return ExternalTextureSourceHost(
      {}, {}, gfx::IntSize(0, 0), gfx::SurfaceFormat::R8G8B8A8,
      gfx::YUVRangedColorSpace::GbrIdentity, {}, {});
}

/* static */ ExternalTextureSourceHost
ExternalTextureSourceHost::CreateFromDXGITextureHost(
    WebGPUParent* aParent, RawId aDeviceId, RawId aQueueId,
    const ExternalTextureSourceDescriptor& aDesc,
    const layers::DXGITextureHostD3D11* aTextureHost) {
#ifdef XP_WIN
  Maybe<HANDLE> handle;
  if (aTextureHost->mGpuProcessTextureId) {
    auto* textureMap = layers::GpuProcessD3D11TextureMap::Get();
    if (textureMap) {
      handle =
          textureMap->GetSharedHandle(aTextureHost->mGpuProcessTextureId.ref());
    }
  } else if (aTextureHost->mHandle) {
    handle.emplace(aTextureHost->mHandle->GetHandle());
  }

  if (!handle) {
    gfxCriticalErrorOnce() << "Failed to obtain D3D texture handle";
    aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                         "Failed to obtain D3D texture handle"_ns);
    return CreateError();
  }

  const gfx::YUVRangedColorSpace colorSpace = gfx::ToYUVRangedColorSpace(
      gfx::ToYUVColorSpace(aTextureHost->mColorSpace),
      aTextureHost->mColorRange, aTextureHost->mTransferFunction);

  ffi::WGPUTextureFormat textureFormat;
  AutoTArray<std::pair<ffi::WGPUTextureFormat, ffi::WGPUTextureAspect>, 2>
      viewFormatAndAspects;
  switch (aTextureHost->mFormat) {
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::R8G8B8X8:
      textureFormat = {ffi::WGPUTextureFormat_Rgba8Unorm};
      viewFormatAndAspects.AppendElement(
          std::make_pair(textureFormat, ffi::WGPUTextureAspect_All));
      break;
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
      textureFormat = {ffi::WGPUTextureFormat_Bgra8Unorm};
      viewFormatAndAspects.AppendElement(
          std::make_pair(textureFormat, ffi::WGPUTextureAspect_All));
      break;
    case gfx::SurfaceFormat::NV12:
      textureFormat = {ffi::WGPUTextureFormat_NV12};
      viewFormatAndAspects.AppendElement(
          std::make_pair(ffi::WGPUTextureFormat{ffi::WGPUTextureFormat_R8Unorm},
                         ffi::WGPUTextureAspect_Plane0));
      viewFormatAndAspects.AppendElement(std::make_pair(
          ffi::WGPUTextureFormat{ffi::WGPUTextureFormat_Rg8Unorm},
          ffi::WGPUTextureAspect_Plane1));
      break;
    case gfx::SurfaceFormat::P010:
      textureFormat = {ffi::WGPUTextureFormat_P010};
      viewFormatAndAspects.AppendElement(std::make_pair(
          ffi::WGPUTextureFormat{ffi::WGPUTextureFormat_R16Unorm},
          ffi::WGPUTextureAspect_Plane0));
      viewFormatAndAspects.AppendElement(std::make_pair(
          ffi::WGPUTextureFormat{ffi::WGPUTextureFormat_Rg16Unorm},
          ffi::WGPUTextureAspect_Plane1));
      break;
    default:
      gfxCriticalNoteOnce << "Unsupported surface format: "
                          << aTextureHost->mFormat;
      aParent->ReportError(
          aDeviceId, dom::GPUErrorFilter::Internal,
          nsPrintfCString("Unsupported surface format: %s",
                          mozilla::ToString(aTextureHost->mFormat).c_str()));
      return CreateError();
  }

  AutoTArray<RawId, 1> usedTextureIds = {aDesc.mTextureIds[0]};
  AutoTArray<RawId, 2> usedViewIds;

  const ffi::WGPUTextureDescriptor textureDesc{
      .size =
          ffi::WGPUExtent3d{
              .width = static_cast<uint32_t>(aTextureHost->mSize.width),
              .height = static_cast<uint32_t>(aTextureHost->mSize.height),
              .depth_or_array_layers = 1,
          },
      .mip_level_count = 1,
      .sample_count = 1,
      .dimension = ffi::WGPUTextureDimension_D2,
      .format = textureFormat,
      .usage = WGPUTextureUsages_TEXTURE_BINDING,
      .view_formats = {},
  };
  {
    ErrorBuffer error;
    ffi::wgpu_server_device_import_texture_from_shared_handle(
        aParent->GetContext(), aDeviceId, usedTextureIds[0], &textureDesc,
        *handle, error.ToFFI());
    // From here on there's no need to return early with `CreateError()` in
    // case of an error, as an error creating a texture or view will be
    // propagated to any views or external textures created from them.
    // Since we have full control over the creation of this texture, any
    // validation error we encounter should be treated as an internal error.
    error.CoerceValidationToInternal();
    aParent->ForwardError(error);
  }

  for (size_t i = 0; i < viewFormatAndAspects.Length(); i++) {
    auto [format, aspect] = viewFormatAndAspects[i];
    ffi::WGPUTextureViewDescriptor viewDesc{
        .format = &format,
        .aspect = aspect,
    };
    {
      ErrorBuffer error;
      ffi::wgpu_server_texture_create_view(aParent->GetContext(), aDeviceId,
                                           usedTextureIds[0], aDesc.mViewIds[i],
                                           &viewDesc, error.ToFFI());
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
    usedViewIds.AppendElement(aDesc.mViewIds[i]);
  }
  ExternalTextureSourceHost source(
      usedTextureIds, usedViewIds, aDesc.mSize, aTextureHost->mFormat,
      colorSpace, aDesc.mSampleTransform, aDesc.mLoadTransform);
  source.mFenceId = aTextureHost->mFencesHolderId;
  return source;
#else
  MOZ_CRASH();
#endif
}

/* static */ ExternalTextureSourceHost
ExternalTextureSourceHost::CreateFromDXGIYCbCrTextureHost(
    WebGPUParent* aParent, RawId aDeviceId, RawId aQueueId,
    const ExternalTextureSourceDescriptor& aDesc,
    const layers::DXGIYCbCrTextureHostD3D11* aTextureHost) {
#ifdef XP_WIN
  const gfx::YUVRangedColorSpace colorSpace = gfx::ToYUVRangedColorSpace(
      aTextureHost->mYUVColorSpace, aTextureHost->mColorRange,
      aTextureHost->mTransferFunction);

  ffi::WGPUTextureFormat planeFormat;
  switch (aTextureHost->mColorDepth) {
    case gfx::ColorDepth::COLOR_8:
      planeFormat = {ffi::WGPUTextureFormat_R8Unorm};
      break;
    case gfx::ColorDepth::COLOR_10:
    case gfx::ColorDepth::COLOR_12:
    case gfx::ColorDepth::COLOR_16:
      gfxCriticalNoteOnce << "Unsupported color depth: "
                          << aTextureHost->mColorDepth;
      aParent->ReportError(
          aDeviceId, dom::GPUErrorFilter::Internal,
          nsPrintfCString(
              "Unsupported color depth: %s",
              mozilla::ToString(aTextureHost->mColorDepth).c_str()));
      return CreateError();
  }

  for (int i = 0; i < 3; i++) {
    {
      const auto size = i == 0 ? aTextureHost->mSizeY : aTextureHost->mSizeCbCr;
      const ffi::WGPUTextureDescriptor textureDesc{
          .size =
              ffi::WGPUExtent3d{
                  .width = static_cast<uint32_t>(size.width),
                  .height = static_cast<uint32_t>(size.height),
                  .depth_or_array_layers = 1,
              },
          .mip_level_count = 1,
          .sample_count = 1,
          .dimension = ffi::WGPUTextureDimension_D2,
          .format = planeFormat,
          .usage = WGPUTextureUsages_TEXTURE_BINDING,
          .view_formats = {},
      };
      ErrorBuffer error;
      ffi::wgpu_server_device_import_texture_from_shared_handle(
          aParent->GetContext(), aDeviceId, aDesc.mTextureIds[i], &textureDesc,
          aTextureHost->mHandles[i]->GetHandle(), error.ToFFI());
      // From here on there's no need to return early with `CreateError()` in
      // case of an error, as an error creating a texture or view will be
      // propagated to any views or external textures created from them.
      // Since we have full control over the creation of this texture, any
      // validation error we encounter should be treated as an internal error.
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
    {
      ffi::WGPUTextureViewDescriptor viewDesc{};
      ErrorBuffer error;
      ffi::wgpu_server_texture_create_view(
          aParent->GetContext(), aDeviceId, aDesc.mTextureIds[i],
          aDesc.mViewIds[i], &viewDesc, error.ToFFI());
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
  }

  ExternalTextureSourceHost source(
      aDesc.mTextureIds, aDesc.mViewIds, aDesc.mSize, aTextureHost->GetFormat(),
      colorSpace, aDesc.mSampleTransform, aDesc.mLoadTransform);
  source.mFenceId = Some(aTextureHost->mFencesHolderId);
  return source;
#else
  MOZ_CRASH();
#endif
}

/* static */ ExternalTextureSourceHost
ExternalTextureSourceHost::CreateFromMacIOSurfaceTextureHost(
    WebGPUParent* aParent, RawId aDeviceId,
    const ExternalTextureSourceDescriptor& aDesc,
    const layers::MacIOSurfaceTextureHostOGL* aTextureHost) {
#ifdef XP_MACOSX
  const RefPtr<MacIOSurface> ioSurface = aTextureHost->mSurface;
  if (!ioSurface) {
    gfxCriticalErrorOnce() << "Failed to lookup MacIOSurface";
    aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                         "Failed to lookup MacIOSurface"_ns);

    return CreateError();
  }

  // mGpuFence should be null. It is only required to synchronize GPU reads
  // from an IOSurface following GPU writes, e.g. when an IOSurface is used for
  // WebGPU presentation. In our case the IOSurface has been written to from
  // the CPU or obtained from a CVPixelBuffer, and no additional synchronization
  // is required.
  MOZ_ASSERT(!aTextureHost->mGpuFence);

  const gfx::SurfaceFormat format = ioSurface->GetFormat();
  const gfx::YUVRangedColorSpace colorSpace = gfx::ToYUVRangedColorSpace(
      ioSurface->GetYUVColorSpace(), ioSurface->GetColorRange(),
      ioSurface->GetTransferFunction());

  auto planeSize = [ioSurface](auto plane) {
    return ffi::WGPUExtent3d{
        .width = static_cast<uint32_t>(ioSurface->GetDevicePixelWidth(plane)),
        .height = static_cast<uint32_t>(ioSurface->GetDevicePixelHeight(plane)),
        .depth_or_array_layers = 1,
    };
  };
  auto yuvPlaneFormat =
      [ioSurface](auto numComponents) -> ffi::WGPUTextureFormat {
    switch (numComponents) {
      case 1:
        switch (ioSurface->GetColorDepth()) {
          case gfx::ColorDepth::COLOR_8:
            return {ffi::WGPUTextureFormat_R8Unorm};
          case gfx::ColorDepth::COLOR_10:
          case gfx::ColorDepth::COLOR_12:
          case gfx::ColorDepth::COLOR_16:
            return {ffi::WGPUTextureFormat_R16Unorm};
        }
      case 2:
        switch (ioSurface->GetColorDepth()) {
          case gfx::ColorDepth::COLOR_8:
            return {ffi::WGPUTextureFormat_Rg8Unorm};
          case gfx::ColorDepth::COLOR_10:
          case gfx::ColorDepth::COLOR_12:
          case gfx::ColorDepth::COLOR_16:
            return {ffi::WGPUTextureFormat_Rg16Unorm};
        }
      default:
        MOZ_CRASH("Invalid numComponents");
    }
  };

  AutoTArray<ffi::WGPUTextureDescriptor, 2> textureDescs;
  switch (format) {
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::R8G8B8X8:
      textureDescs.AppendElement(ffi::WGPUTextureDescriptor{
          .size = planeSize(0),
          .mip_level_count = 1,
          .sample_count = 1,
          .dimension = ffi::WGPUTextureDimension_D2,
          .format = {ffi::WGPUTextureFormat_Rgba8Unorm},
          .usage = WGPUTextureUsages_TEXTURE_BINDING,
          .view_formats = {},
      });
      break;
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
      textureDescs.AppendElement(ffi::WGPUTextureDescriptor{
          .size = planeSize(0),
          .mip_level_count = 1,
          .sample_count = 1,
          .dimension = ffi::WGPUTextureDimension_D2,
          .format = {ffi::WGPUTextureFormat_Bgra8Unorm},
          .usage = WGPUTextureUsages_TEXTURE_BINDING,
          .view_formats = {},
      });
      break;
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::P010: {
      textureDescs.AppendElement(ffi::WGPUTextureDescriptor{
          .size = planeSize(0),
          .mip_level_count = 1,
          .sample_count = 1,
          .dimension = ffi::WGPUTextureDimension_D2,
          .format = yuvPlaneFormat(1),
          .usage = WGPUTextureUsages_TEXTURE_BINDING,
          .view_formats = {},
      });
      textureDescs.AppendElement(ffi::WGPUTextureDescriptor{
          .size = planeSize(1),
          .mip_level_count = 1,
          .sample_count = 1,
          .dimension = ffi::WGPUTextureDimension_D2,
          .format = yuvPlaneFormat(2),
          .usage = WGPUTextureUsages_TEXTURE_BINDING,
          .view_formats = {},
      });
    } break;
    default:
      gfxCriticalErrorOnce() << "Unsupported IOSurface format: " << format;
      aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                           nsPrintfCString("Unsupported IOSurface format: %s",
                                           mozilla::ToString(format).c_str()));
      return CreateError();
  }

  AutoTArray<RawId, 2> usedTextureIds;
  AutoTArray<RawId, 2> usedViewIds;
  for (size_t i = 0; i < textureDescs.Length(); i++) {
    usedTextureIds.AppendElement(aDesc.mTextureIds[i]);
    usedViewIds.AppendElement(aDesc.mViewIds[i]);
    {
      ErrorBuffer error;
      ffi::wgpu_server_device_import_texture_from_iosurface(
          aParent->GetContext(), aDeviceId, aDesc.mTextureIds[i],
          &textureDescs[i], ioSurface->GetIOSurfaceID(), i, error.ToFFI());
      // From here on there's no need to return early with `CreateError()` in
      // case of an error, as an error creating a texture or view will be
      // propagated to any views or external textures created from them.
      // Since we have full control over the creation of this texture, any
      // validation error we encounter should be treated as an internal error.
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
    ffi::WGPUTextureViewDescriptor viewDesc{};
    {
      ErrorBuffer error;
      ffi::wgpu_server_texture_create_view(
          aParent->GetContext(), aDeviceId, aDesc.mTextureIds[i],
          aDesc.mViewIds[i], &viewDesc, error.ToFFI());
      error.CoerceValidationToInternal();
      aParent->ForwardError(error);
    }
  }
  return ExternalTextureSourceHost(usedTextureIds, usedViewIds, aDesc.mSize,
                                   format, colorSpace, aDesc.mSampleTransform,
                                   aDesc.mLoadTransform);
#else
  MOZ_CRASH();
#endif
}

static color::ColorspaceTransform GetColorSpaceTransform(
    gfx::YUVRangedColorSpace aSrcColorSpace,
    ffi::WGPUPredefinedColorSpace aDestColorSpace) {
  const bool rec709GammaAsSrgb =
      StaticPrefs::gfx_color_management_rec709_gamma_as_srgb();
  const bool rec2020GammaAsRec709 =
      StaticPrefs::gfx_color_management_rec2020_gamma_as_rec709();

  color::ColorspaceDesc srcColorSpace;
  switch (aSrcColorSpace) {
    case gfx::YUVRangedColorSpace::BT601_Narrow:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec601_525_Ntsc(),
          .tf = rec709GammaAsSrgb ? color::TransferFunctionDesc::Srgb()
                                  : color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec601(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT601_Full:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec601_525_Ntsc(),
          .tf = rec709GammaAsSrgb ? color::TransferFunctionDesc::Srgb()
                                  : color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec601(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT709_Narrow:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec709(),
          .tf = rec709GammaAsSrgb ? color::TransferFunctionDesc::Srgb()
                                  : color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT709_Full:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec709(),
          .tf = rec709GammaAsSrgb ? color::TransferFunctionDesc::Srgb()
                                  : color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec709(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2020_Narrow:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = rec2020GammaAsRec709 && rec709GammaAsSrgb
                    ? color::TransferFunctionDesc::Srgb()
                    : (rec2020GammaAsRec709
                           ? color::TransferFunctionDesc::Rec709()
                           : color::TransferFunctionDesc::Rec2020_12bit()),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2020_Full:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = rec2020GammaAsRec709 && rec709GammaAsSrgb
                    ? color::TransferFunctionDesc::Srgb()
                    : (rec2020GammaAsRec709
                           ? color::TransferFunctionDesc::Rec709()
                           : color::TransferFunctionDesc::Rec2020_12bit()),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2100_HLG_Narrow:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_HLG(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2100_HLG_Full:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_HLG(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2100_PQ_Narrow:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_PQ(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Narrow8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::BT2100_PQ_Full:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec2020(),
          .tf = color::TransferFunctionDesc::Rec2100_PQ(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Rec2020(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
    case gfx::YUVRangedColorSpace::GbrIdentity:
      srcColorSpace = {
          .chrom = color::Chromaticities::Rec709(),
          .tf = color::TransferFunctionDesc::Rec709(),
          .yuv =
              color::YuvDesc{
                  .yCoeffs = color::YuvLumaCoeffs::Gbr(),
                  .ycbcr = color::YcbcrDesc::Full8(),
              },
      };
      break;
  }

  color::ColorspaceDesc destColorSpace{};
  switch (aDestColorSpace) {
    case ffi::WGPUPredefinedColorSpace_Srgb:
      destColorSpace = {.chrom = color::Chromaticities::Srgb(),
                        .tf = color::TransferFunctionDesc::Srgb()};
      break;
    case ffi::WGPUPredefinedColorSpace_DisplayP3:
      destColorSpace = {.chrom = color::Chromaticities::DisplayP3(),
                        .tf = color::TransferFunctionDesc::DisplayP3()};
      break;
    case ffi::WGPUPredefinedColorSpace_Sentinel:
      MOZ_CRASH("Invalid WGPUPredefinedColorSpace");
  }

  return color::ColorspaceTransform::Create(srcColorSpace, destColorSpace);
}

static ffi::WGPUExternalTextureFormat MapFormat(gfx::SurfaceFormat aFormat) {
  switch (aFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::R8G8B8X8:
      return ffi::WGPUExternalTextureFormat_Rgba;
    case gfx::SurfaceFormat::YUV420:
      return ffi::WGPUExternalTextureFormat_Yu12;
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::P010:
      return ffi::WGPUExternalTextureFormat_Nv12;
    default:
      MOZ_CRASH("Unexpected SurfaceFormat");
  }
}

// TODO: support HLG and PQ https://bugzilla.mozilla.org/show_bug.cgi?id=2024870
static ffi::WGPUExternalTextureTransferFunction MapTransferFunction(
    std::optional<color::TransferFunctionDesc> aTf) {
  if (aTf) {
    return ffi::WGPUExternalTextureTransferFunction{
        .a = aTf->a,
        .b = aTf->b,
        .g = aTf->g,
        .k = aTf->k,
    };
  } else {
    return ffi::WGPUExternalTextureTransferFunction{
        .a = 1.0,
        .b = 1.0,
        .g = 1.0,
        .k = 1.0,
    };
  }
}

ffi::WGPUExternalTextureDescriptorFromSource
ExternalTextureSourceHost::GetExternalTextureDescriptor(
    ffi::WGPUPredefinedColorSpace aDestColorSpace) const {
  ffi::WGPUExternalTextureDescriptorFromSource desc;

  desc.planes = ffi::WGPUFfiSlice_TextureViewId{
      .data = mViewIds.Elements(),
      .length = mViewIds.Length(),
  };
  desc.width = static_cast<uint32_t>(mSize.width);
  desc.height = static_cast<uint32_t>(mSize.height);
  desc.format = MapFormat(mFormat);

  auto colorSpaceTransform =
      GetColorSpaceTransform(mColorSpace, aDestColorSpace);
  // Makes a generator for a color::mat that yields its components in
  // column-major order
  auto make_column_major_generator = [](auto mat) {
    return [i = 0, mat]() mutable {
      auto val = mat.at(i / mat.y_rows, i % mat.y_rows);
      i++;
      return val;
    };
  };
  std::generate(
      std::begin(desc.yuv_conversion_matrix),
      std::end(desc.yuv_conversion_matrix),
      make_column_major_generator(colorSpaceTransform.srcRgbTfFromSrc));
  std::generate(
      std::begin(desc.gamut_conversion_matrix),
      std::end(desc.gamut_conversion_matrix),
      make_column_major_generator(colorSpaceTransform.dstRgbLinFromSrcRgbLin));
  desc.src_transfer_function = MapTransferFunction(colorSpaceTransform.srcTf);
  desc.dst_transfer_function = MapTransferFunction(colorSpaceTransform.dstTf);
  std::copy(mSampleTransform.begin(), mSampleTransform.end(),
            desc.sample_transform);
  std::copy(mLoadTransform.begin(), mLoadTransform.end(), desc.load_transform);

  return desc;
}

bool ExternalTextureSourceHost::OnBeforeQueueSubmit(WebGPUParent* aParent,
                                                    RawId aDeviceId,
                                                    RawId aQueueId) {
#if defined(XP_WIN)
  // Wait on the write fence provided by the decoder, if any, to ensure we don't
  // read from the texture before writes have completed.
  if (mFenceId) {
    const auto* fencesMap = layers::CompositeProcessD3D11FencesHolderMap::Get();
    if (!fencesMap) {
      gfxCriticalErrorOnce()
          << "CompositeProcessD3D11FencesHolderMap is not initialized";
      aParent->ReportError(
          aDeviceId, dom::GPUErrorFilter::Internal,
          "CompositeProcessD3D11FencesHolderMap is not initialized"_ns);
      return false;
    }
    auto [fenceHandle, fenceValue] =
        fencesMap->GetWriteFenceHandleAndValue(*mFenceId);
    if (fenceHandle) {
      const bool success =
          ffi::wgpu_server_device_wait_fence_from_shared_handle(
              aParent->GetContext(), aDeviceId, aQueueId,
              fenceHandle->GetHandle(), fenceValue);
      if (success) {
        // No need to wait next time
        mFenceId.reset();
      } else {
        gfxCriticalErrorOnce() << "Failed to wait on write fence";
        aParent->ReportError(aDeviceId, dom::GPUErrorFilter::Internal,
                             "Failed to wait on write fence"_ns);
        return false;
      }
    }
  }
  return true;
#else
  return true;
#endif
}

}  // namespace mozilla::webgpu
