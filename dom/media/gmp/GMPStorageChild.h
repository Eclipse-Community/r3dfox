/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPStorageChild_h_
#define GMPStorageChild_h_

#include "mozilla/gmp/PGMPStorageChild.h"
#include "mozilla/Mutex.h"
#include "mozilla/RecursiveMutex.h"
#include "gmp-storage.h"
#include "nsTHashtable.h"
#include "nsRefPtrHashtable.h"
#include "gmp-platform.h"

#include <queue>

namespace mozilla::gmp {

class GMPChild;
class GMPStorageChild;

class GMPRecordImpl final : public GMPRecord {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GMPRecordImpl)

  GMPRecordImpl(GMPStorageChild* aOwner, const nsCString& aName,
                GMPRecordClient* aClient);

  void DestroyOwner();

  // GMPRecord.
  GMPErr Open() override;
  GMPErr Read() override;
  GMPErr Write(const uint8_t* aData, uint32_t aDataSize) override;
  GMPErr Close() override;

  const nsCString& Name() const { return mName; }

  void OpenComplete(GMPErr aStatus);
  void ReadComplete(GMPErr aStatus, const uint8_t* aBytes, uint32_t aLength);
  void WriteComplete(GMPErr aStatus);

 private:
  RefPtr<GMPStorageChild> GetOwner();

  ~GMPRecordImpl() = default;
  RecursiveMutex mMutex{"GMPRecordImpl"};
  const nsCString mName;
  GMPRecordClient* mClient MOZ_GUARDED_BY(mMutex);
  GMPStorageChild* mOwner MOZ_GUARDED_BY(mMutex);
};

class GMPStorageChild final : public PGMPStorageChild {
  friend class PGMPStorageChild;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GMPStorageChild)

  explicit GMPStorageChild(GMPChild* aPlugin);

  GMPErr CreateRecord(const nsCString& aRecordName, GMPRecord** aOutRecord,
                      GMPRecordClient* aClient);

  GMPErr Open(GMPRecordImpl* aRecord);

  GMPErr Read(GMPRecordImpl* aRecord);

  GMPErr Write(GMPRecordImpl* aRecord, const uint8_t* aData,
               uint32_t aDataSize);

  GMPErr Close(const nsCString& aRecordName);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  bool HasRecord(const nsCString& aRecordName) MOZ_REQUIRES(mMutex);
  already_AddRefed<GMPRecordImpl> GetRecord(const nsCString& aRecordName);

 protected:
  ~GMPStorageChild() = default;

  // PGMPStorageChild
  mozilla::ipc::IPCResult RecvOpenComplete(const nsCString& aRecordName,
                                           const GMPErr& aStatus);
  mozilla::ipc::IPCResult RecvReadComplete(const nsCString& aRecordName,
                                           const GMPErr& aStatus,
                                           nsTArray<uint8_t>&& aBytes);
  mozilla::ipc::IPCResult RecvWriteComplete(const nsCString& aRecordName,
                                            const GMPErr& aStatus);
  mozilla::ipc::IPCResult RecvShutdown();

 private:
  Mutex mMutex{"GMPStorageChild"};
  nsRefPtrHashtable<nsCStringHashKey, GMPRecordImpl> mRecords
      MOZ_GUARDED_BY(mMutex);
  GMPChild* mPlugin;
  bool mShutdown MOZ_GUARDED_BY(mMutex) = false;
};

}  // namespace mozilla::gmp

#endif  // GMPStorageChild_h_
