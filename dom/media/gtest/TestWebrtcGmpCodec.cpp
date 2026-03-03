/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "api/video/i420_buffer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwebrtcglue/WebrtcGmpVideoCodec.h"
#include "media/base/media_constants.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsServiceManagerUtils.h"

using testing::_;
using testing::AtLeast;
using testing::Eq;
using testing::Ge;
using testing::Gt;
using testing::Property;
using testing::Test;

namespace mozilla {

struct TestWebrtcGmpVideoEncoder : public Test {
  nsCOMPtr<nsIThread> mGmpThread;
  RefPtr<WebrtcGmpVideoEncoder> mEncoder;
  webrtc::VideoCodec mCodecSettings;
  webrtc::VideoEncoder::Settings mSettings = {
      webrtc::VideoEncoder::Capabilities(/*loss_notification=*/true),
      /*number_of_cores=*/1, /*max_payload_size=*/0};

  void SetUp() override {
    mEncoder = MakeRefPtr<WebrtcGmpVideoEncoder>(
        webrtc::SdpVideoFormat(cricket::kH264CodecName), "dummy");
    mCodecSettings.codecType = webrtc::VideoCodecType::kVideoCodecH264;
    mCodecSettings.numberOfSimulcastStreams = 1;
    mCodecSettings.simulcastStream[0].active = true;
    mCodecSettings.simulcastStream[0].numberOfTemporalLayers = 1;
    mCodecSettings.width = 640;
    mCodecSettings.height = 480;
    mCodecSettings.maxFramerate = 60;
    mCodecSettings.minBitrate = 50;
    mCodecSettings.startBitrate = 200;
    mCodecSettings.maxBitrate = 1000;

    nsCOMPtr<mozIGeckoMediaPluginService> mps =
        do_GetService("@mozilla.org/gecko-media-plugin-service;1");
    ASSERT_TRUE(mps);
    mps->GetThread(getter_AddRefs(mGmpThread));
    ASSERT_TRUE(mGmpThread);
  }

  void TearDown() override { mEncoder = nullptr; }
};

struct MockEncodedImageCallback : public webrtc::EncodedImageCallback {
  MOCK_METHOD(Result, OnEncodedImage,
              (const webrtc::EncodedImage&, const webrtc::CodecSpecificInfo*),
              (override));
  MOCK_METHOD(void, OnDroppedFrame, (DropReason), (override));
};

auto CreateBlackFrame(int width, int height) {
  auto buffer = webrtc::I420Buffer::Create(width, height);
  webrtc::I420Buffer::SetBlack(buffer.get());
  return buffer;
}

TEST_F(TestWebrtcGmpVideoEncoder, EmptyLifecycle) {}

TEST_F(TestWebrtcGmpVideoEncoder, InitEncode) {
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());
}

TEST_F(TestWebrtcGmpVideoEncoder, Encode) {
  using Result = webrtc::EncodedImageCallback::Result;
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());

  MozPromiseHolder<GenericPromise> doneHolder;
  RefPtr donePromise = doneHolder.Ensure(__func__);
  MockEncodedImageCallback callback;
  constexpr uint32_t rtp_time = 55;
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          Property(&webrtc::EncodedImage::RtpTimestamp, Eq(rtp_time)), _))
      .WillOnce([&] {
        doneHolder.Resolve(true, "TestWebrtcGmpVideoEncoder::Encode");
        return Result(Result::OK);
      });
  mEncoder->RegisterEncodeCompleteCallback(&callback);
  std::vector<webrtc::VideoFrameType> types = {
      webrtc::VideoFrameType::kVideoFrameKey};
  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_rtp_timestamp(rtp_time)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(WaitForResolve(donePromise), true);
}

TEST_F(TestWebrtcGmpVideoEncoder, BackPressure) {
  using Result = webrtc::EncodedImageCallback::Result;
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());

  MozPromiseHolder<GenericPromise> doneHolder;
  RefPtr donePromise = doneHolder.Ensure(__func__);
  MockEncodedImageCallback callback;
  constexpr uint32_t rtpTime = 55;
  constexpr size_t iterations = 1000;
  Atomic<uint32_t> lastRtpTime{};
  Atomic<size_t> eventCount{};
  const auto countIteration = [&] {
    size_t c = ++eventCount;
    EXPECT_LE(c, iterations);
    if (c == iterations) {
      doneHolder.Resolve(true, "TestWebrtcGmpVideoEncoder::BackPressure");
    }
  };
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          Property(&webrtc::EncodedImage::RtpTimestamp,
                   testing::AllOf(Ge(rtpTime), Gt<uint32_t>(lastRtpTime))),
          _))
      .Times(AtLeast(1))
      .WillRepeatedly([&](const auto& aImage, const auto*) {
        lastRtpTime = aImage.RtpTimestamp();
        countIteration();
        return Result(Result::OK);
      });
  EXPECT_CALL(
      callback,
      OnDroppedFrame(MockEncodedImageCallback::DropReason::kDroppedByEncoder))
      .Times(AtLeast(iterations / 10))
      .WillRepeatedly(countIteration);
  mEncoder->RegisterEncodeCompleteCallback(&callback);
  std::vector<webrtc::VideoFrameType> types = {
      webrtc::VideoFrameType::kVideoFrameKey};
  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_rtp_timestamp(rtpTime)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);
  for (size_t i = 1; i < iterations; ++i) {
    mEncoder->Encode(webrtc::VideoFrame::Builder()
                         .set_rtp_timestamp(rtpTime + i)
                         .set_video_frame_buffer(CreateBlackFrame(
                             mCodecSettings.width, mCodecSettings.height))
                         .build(),
                     &types);
  }
  EXPECT_EQ(WaitForResolve(donePromise), true);
  EXPECT_EQ(eventCount, iterations);
}

TEST_F(TestWebrtcGmpVideoEncoder, ReUse) {
  using Result = webrtc::EncodedImageCallback::Result;
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());

  MozPromiseHolder<GenericPromise> doneHolder;
  RefPtr donePromise = doneHolder.Ensure(__func__);
  MockEncodedImageCallback callback;
  constexpr uint32_t rtpTime = 55;
  constexpr uint32_t rtpTime2 = rtpTime * 2;
  EXPECT_CALL(callback,
              OnEncodedImage(
                  Property(&webrtc::EncodedImage::RtpTimestamp, rtpTime2), _))
      .WillOnce([&] {
        doneHolder.Resolve(true, "TestWebrtcGmpVideoEncoder::ReUse");
        return Result(Result::OK);
      });
  mEncoder->RegisterEncodeCompleteCallback(&callback);

  // Block the GMP thread until after Shutdown() as to avoid racing between the
  // first encoded callback and the Shutdown() call.
  Monitor mon(__func__);
  bool block = true;
  MOZ_ALWAYS_SUCCEEDS(
      mGmpThread->Dispatch(NS_NewRunnableFunction(__func__, [&] {
        MonitorAutoLock lock(mon);
        while (block) {
          lock.Wait();
        }
      })));
  std::vector<webrtc::VideoFrameType> types = {
      webrtc::VideoFrameType::kVideoFrameKey};
  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_rtp_timestamp(rtpTime)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);

  // Shutdown mid-encode, then re-init and encode again.
  mEncoder->Shutdown();
  {
    MonitorAutoLock lock(mon);
    block = false;
    lock.Notify();
  }
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());
  mEncoder->RegisterEncodeCompleteCallback(&callback);

  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_rtp_timestamp(rtpTime2)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(WaitForResolve(donePromise), true);
}

TEST_F(TestWebrtcGmpVideoEncoder, TrackedFrameDrops) {
  using Result = webrtc::EncodedImageCallback::Result;
  // Tell the fakeopenh264 plugin to drop some allocated input frames without
  // telling us. It will drop every second input frame. This shall get tracked
  // as frame drops.
  mCodecSettings.SetFrameDropEnabled(true);
  mEncoder->InitEncode(&mCodecSettings, mSettings);
  WaitFor(*mEncoder->InitPluginEvent());

  MozPromiseHolder<GenericPromise> encodedHolder, encodedHolder2;
  RefPtr encodedPromise = encodedHolder.Ensure(__func__);
  MockEncodedImageCallback callback;
  constexpr uint32_t ntpTime = 55;
  constexpr uint32_t ntpTime2 = ntpTime * 2;
  EXPECT_CALL(
      callback,
      OnEncodedImage(Property(&webrtc::EncodedImage::NtpTimeMs, ntpTime), _))
      .WillOnce([&] {
        encodedHolder.Resolve(true,
                              "TestWebrtcGmpVideoEncoder::TrackedFrameDrops");
        return Result(Result::OK);
      });
  EXPECT_CALL(
      callback,
      OnDroppedFrame(MockEncodedImageCallback::DropReason::kDroppedByEncoder))
      .WillOnce([&] {
        encodedHolder2.Resolve(false,
                               "TestWebrtcGmpVideoEncoder::TrackedFrameDrops");
      });
  mEncoder->RegisterEncodeCompleteCallback(&callback);

  std::vector<webrtc::VideoFrameType> types = {
      webrtc::VideoFrameType::kVideoFrameKey};
  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_ntp_time_ms(ntpTime)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(WaitForResolve(encodedPromise), true);
  encodedPromise = encodedHolder2.Ensure(__func__);
  EXPECT_EQ(
      mEncoder->Encode(webrtc::VideoFrame::Builder()
                           .set_ntp_time_ms(ntpTime2)
                           .set_video_frame_buffer(CreateBlackFrame(
                               mCodecSettings.width, mCodecSettings.height))
                           .build(),
                       &types),
      WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(WaitForResolve(encodedPromise), false);
}
}  // namespace mozilla
