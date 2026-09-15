/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "WinUtils.h"

#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsNetUtil.h"
#include "nsStreamUtils.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/SpinEventLoopUntil.h"

using namespace mozilla;
using namespace mozilla::widget;

static LayoutDeviceIntRegion GetTestRegion() {
  LayoutDeviceIntRegion region;
  region.OrWith(LayoutDeviceIntRect(0, 0, 10, 10));
  region.OrWith(LayoutDeviceIntRect(15, 15, 50, 50));
  return region;
}

TEST(WinUtils, Regions)
{
  auto region = GetTestRegion();
  nsAutoRegion rgn(WinUtils::RegionToHRGN(region));
  ASSERT_NE(rgn, nullptr) << "Conversion should succeed";
  ASSERT_EQ(region, WinUtils::ConvertHRGNToRegion(rgn))
      << "Region should round-trip";
}

/*****************************************************************************/

// https://learn.microsoft.com/en-us/dotnet/api/system.security.securityzone?view=netframework-4.8.1
enum Zone {
  ZONE_MY_COMPUTER = 0u,
  ZONE_INTRANET = 1u,
  ZONE_TRUSTED = 2u,
  ZONE_INTERNET = 3u,
  ZONE_RESTRICTED = 4u
};

// ZoneIdKey overrides applied by MaybeWriteFileZoneId.
constexpr auto kFallbackUrl = "about:internet"_ns;
constexpr auto kNoReferrerLine = ""_ns;

struct FileTestData {
  nsCString mSourceUrl;
  nsCString mReferrerSpec;
  // Set to Nothing() to check the case for file-does-not-exist.
  Maybe<uint32_t> mExpectedZone;
  // Overrides for the HostUrl / ReferrerUrl values written to the ADS.
  //   Nothing() -> expected equals the input
  //   Some(s)   -> the line is expected to be exactly "<key>=<s>\r\n"
  Maybe<nsCString> mExpectedHostUrl = Nothing();
  Maybe<nsCString> mExpectedReferrer = Nothing();
  // When false (e.g. private browsing), no HostUrl/ReferrerUrl is written.
  bool mShouldStoreUrls = true;
};

#define CheckSuccess(_operation)                               \
  {                                                            \
    nsresult rv = _operation;                                  \
    if (NS_FAILED(rv)) {                                       \
      ADD_FAILURE() << #_operation << " | Failed with result " \
                    << static_cast<uint32_t>(rv) << std::endl; \
      return rv;                                               \
    }                                                          \
  }

// Reads the file's Zone.Identifier ADS and asserts it matches what
// MaybeWriteFileZoneIdSync would have written for the given test data.
static nsresult VerifyZoneIdentifier(nsIFile* aSaveFile,
                                     const FileTestData& aData) {
  nsString savePath;
  CheckSuccess(aSaveFile->GetPath(savePath));
  auto isSlash = [](char aCh) { return aCh == '/' || aCh == '\\'; };
  bool isUNC =
      savePath.Length() >= 2 && isSlash(savePath[0]) && isSlash(savePath[1]);
  nsString adsPath = isUNC ? u"\\\\?\\UNC\\"_ns + Substring(savePath, 2)
                           : u"\\\\?\\"_ns + savePath;
  adsPath += u":Zone.Identifier"_ns;

  nsCOMPtr<nsIFile> adsFile;
  CheckSuccess(NS_NewLocalFile(adsPath, getter_AddRefs(adsFile)));

  bool expectWritten =
      aData.mExpectedZone && *aData.mExpectedZone >= Zone::ZONE_INTERNET;

  nsCOMPtr<nsIInputStream> stream;
  nsresult openRv = NS_NewLocalFileInputStream(getter_AddRefs(stream), adsFile);

  if (!expectWritten) {
    // ADS should not have been written.
    EXPECT_EQ(openRv, NS_ERROR_FILE_NOT_FOUND)
        << "Expected no Zone.Identifier ADS";
    return NS_OK;
  }

  CheckSuccess(openRv);

  nsAutoCString actual;
  CheckSuccess(NS_ConsumeStream(stream, UINT32_MAX, actual));

  nsCString expectedHost = aData.mExpectedHostUrl.valueOr(aData.mSourceUrl);
  nsCString expectedReferrer =
      aData.mExpectedReferrer.valueOr(aData.mReferrerSpec);

  nsAutoCString expected;
  expected.AppendLiteral("[ZoneTransfer]\r\n");
  expected.AppendPrintf("ZoneId=%u\r\n", *aData.mExpectedZone);
  if (aData.mShouldStoreUrls) {
    if (!expectedReferrer.IsEmpty()) {
      expected.AppendLiteral("ReferrerUrl=");
      expected.Append(expectedReferrer);
      expected.AppendLiteral("\r\n");
    }
    if (!expectedHost.IsEmpty()) {
      expected.AppendLiteral("HostUrl=");
      expected.Append(expectedHost);
      expected.AppendLiteral("\r\n");
    }
  }

  EXPECT_STREQ(expected.get(), actual.get())
      << "Zone.Identifier contents did not match expected";
  return NS_OK;
}

static nsresult SetAndTestFileZone(const FileTestData& aData) {
  SCOPED_TRACE(testing::Message()
               << "source=" << aData.mSourceUrl.get()
               << " referrer=" << aData.mReferrerSpec.get()
               << " shouldStoreUrls=" << aData.mShouldStoreUrls
               << " expectedZone="
               << (aData.mExpectedZone ? std::to_string(*aData.mExpectedZone)
                                       : std::string("(none)")));

  nsCOMPtr<nsIFile> tmp;
  CheckSuccess(NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(tmp)));
  MOZ_ASSERT(tmp);

  nsID id = nsID::GenerateUUID();
  CheckSuccess(tmp->AppendNative("TestZones-"_ns +
                                 nsCString(id.ToString().get()) + ".txt"_ns));
  if (aData.mExpectedZone) {
    CheckSuccess(tmp->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0666));
  } else {
    bool exists;
    CheckSuccess(tmp->Exists(&exists));
    EXPECT_FALSE(exists);
  }

  bool done = false;
  RefPtr<nsIURI> sourceURI;
  CheckSuccess(NS_NewURI(getter_AddRefs(sourceURI), aData.mSourceUrl));
  RefPtr<nsIReferrerInfo> referrerInfo;
  if (!aData.mReferrerSpec.IsEmpty()) {
    referrerInfo = MakeRefPtr<mozilla::dom::ReferrerInfo>(
        nullptr, mozilla::dom::ReferrerPolicy::_empty, true,
        Some(aData.mReferrerSpec));
  }
  WinUtils::MaybeWriteFileZoneId(tmp, sourceURI, referrerInfo,
                                 aData.mShouldStoreUrls)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [&done, &aData, tmp](bool aResult) {
            if (!aData.mExpectedZone) {
              // File-doesn't-exist case: MaybeWriteFileZoneIdSync returns
              // Ok(false) (it does not consider that an error).
              EXPECT_FALSE(aResult);
            } else if (*aData.mExpectedZone >= Zone::ZONE_INTERNET) {
              EXPECT_TRUE(aResult);
            } else {
              // Below-Internet zones (My Computer, Intranet, Trusted) skip the
              // write per WinUtils::MaybeWriteFileZoneIdSync.
              EXPECT_FALSE(aResult);
            }
            VerifyZoneIdentifier(tmp, aData);
            done = true;
          },
          [&done](nsresult aErr) {
            ADD_FAILURE() << "Unexpected promise rejection: "
                          << static_cast<uint32_t>(aErr);
            done = true;
          });

  SpinEventLoopUntil("write zone test"_ns, [&done]() { return done; });

  // Cleanup.  Skip the file-doesn't-exist rows -- we never created the file.
  if (aData.mExpectedZone) {
    uint32_t dummy;
    CheckSuccess(tmp->Remove(false, &dummy));
  }
  return NS_OK;
}

TEST(WinUtils, MaybeWriteFileZoneId)
{
  const FileTestData fileTestDatas[] = {
      // Valid URL, empty referrer.
      {"http://www.example.org/"_ns, ""_ns, Some(Zone::ZONE_INTERNET)},
      {"about:blank"_ns, ""_ns, Some(Zone::ZONE_INTERNET), Some(kFallbackUrl)},
      {"about:srcdoc"_ns, ""_ns, Some(Zone::ZONE_INTERNET), Some(kFallbackUrl)},
      {"data:text/html,foo"_ns, ""_ns, Some(Zone::ZONE_INTERNET),
       Some(kFallbackUrl)},
      {"http://localhost:8080/"_ns, ""_ns, Some(Zone::ZONE_INTRANET)},
      {"http://127.0.0.1/"_ns, ""_ns, Some(Zone::ZONE_INTERNET)},
      {"file://C:/foo"_ns, ""_ns, Some(Zone::ZONE_MY_COMPUTER),
       Some(kFallbackUrl)},
      {"file:///C:/foo"_ns, ""_ns, Some(Zone::ZONE_MY_COMPUTER),
       Some(kFallbackUrl)},
      {"file://server/share/foo"_ns, ""_ns, Some(Zone::ZONE_INTRANET),
       Some(kFallbackUrl)},
      {"file:////server/share/foo"_ns, ""_ns, Some(Zone::ZONE_INTRANET),
       Some(kFallbackUrl)},

      // Valid URL and referrer.
      {"http://www.example.org/"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET)},
      {"about:blank"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET), Some(kFallbackUrl)},
      {"about:srcdoc"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET), Some(kFallbackUrl)},
      {"data:text/html,foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET), Some(kFallbackUrl)},
      {"http://localhost:8080/"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTRANET)},
      {"http://127.0.0.1/"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET)},
      {"file://C:/foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_MY_COMPUTER), Some(kFallbackUrl)},
      {"file:///C:/foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_MY_COMPUTER), Some(kFallbackUrl)},
      {"file://server/share/foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTRANET), Some(kFallbackUrl)},
      {"file:////server/share/foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTRANET), Some(kFallbackUrl)},

      // Userpass on the source URL is stripped from URL and referrer.
      {"http://user:pw@www.example.org/"_ns, ""_ns, Some(Zone::ZONE_INTERNET),
       Some("http://www.example.org/"_ns)},
      {"http://www.example.org/"_ns, "http://user:pw@www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET), Nothing() /* mExpectedHostUrl */,
       Some("http://www.example.com/"_ns) /* mExpectedReferrer */},

      // Non-permitted scheme on the referrer => ReferrerUrl line omitted.
      {"http://www.example.org/"_ns, "about:blank"_ns,
       Some(Zone::ZONE_INTERNET), Nothing() /* mExpectedHostUrl */,
       Some(kNoReferrerLine) /* mExpectedReferrer */},

      // Private browsing: shouldStoreUrls=false suppresses HostUrl and
      // ReferrerUrl even when both are supplied.
      {"http://www.example.org/"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTERNET), Nothing(), Nothing(),
       /* mShouldStoreUrls */ false},
      {"http://localhost:8080/"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_INTRANET), Nothing(), Nothing(),
       /* mShouldStoreUrls */ false},
      {"file:///C:/foo"_ns, "http://www.example.com/"_ns,
       Some(Zone::ZONE_MY_COMPUTER), Nothing(), Nothing(),
       /* mShouldStoreUrls */ false},

      // File to set zone for does not exist.
      {"http://www.example.org/"_ns, "http://www.example.com/"_ns, Nothing()},
  };

  for (auto& data : fileTestDatas) {
    SetAndTestFileZone(data);
  }
}
