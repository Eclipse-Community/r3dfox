/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPStorageChild.h"
#include "GMPChild.h"
#include "gmp-storage.h"
#include "base/task.h"

#define ON_GMP_THREAD() (mPlugin->GMPMessageLoop() == MessageLoop::current())

#define CALL_ON_GMP_THREAD(_func, ...)                        \
  do {                                                        \
    if (ON_GMP_THREAD()) {                                    \
      _func(__VA_ARGS__);                                     \
    } else {                                                  \
      mPlugin->GMPMessageLoop()->PostTask(                    \
          dont_add_new_uses_of_this::NewRunnableMethod(       \
              this, &GMPStorageChild::_func, ##__VA_ARGS__)); \
    }                                                         \
  } while (false)

namespace mozilla::gmp {

static nsTArray<uint8_t> ToArray(const uint8_t* aData, uint32_t aDataSize) {
  nsTArray<uint8_t> data;
  data.AppendElements(aData, aDataSize);
  return data;
}

GMPRecordImpl::GMPRecordImpl(GMPStorageChild* aOwner, const nsCString& aName,
                             GMPRecordClient* aClient)
    : mName(aName), mClient(aClient), mOwner(aOwner) {}

RefPtr<GMPStorageChild> GMPRecordImpl::GetOwner() {
  RecursiveMutexAutoLock lock(mMutex);
  return RefPtr{mOwner};
}

GMPErr GMPRecordImpl::Open() {
  // The IPDL actor might be destroyed on another thread, releasing the owner,
  // so take a strong reference to ensure it stays alive long enough.
  if (auto owner = GetOwner()) {
    return owner->Open(this);
  }
  return GMPClosedErr;
}

void GMPRecordImpl::OpenComplete(GMPErr aStatus) {
  // While we know mClient is not yet destroyed if the pointer is still valid
  // while holding the lock, be careful to note that it may be destroyed by the
  // plugin after the callback returns.
  RecursiveMutexAutoLock lock(mMutex);
  if (mClient) {
    mClient->OpenComplete(aStatus);
  }
}

GMPErr GMPRecordImpl::Read() {
  // The IPDL actor might be destroyed on another thread, releasing the owner,
  // so take a strong reference to ensure it stays alive long enough.
  if (auto owner = GetOwner()) {
    return owner->Read(this);
  }
  return GMPClosedErr;
}

void GMPRecordImpl::ReadComplete(GMPErr aStatus, const uint8_t* aBytes,
                                 uint32_t aLength) {
  // While we know mClient is not yet destroyed if the pointer is still valid
  // while holding the lock, be careful to note that it may be destroyed by the
  // plugin after the callback returns.
  RecursiveMutexAutoLock lock(mMutex);
  if (mClient) {
    mClient->ReadComplete(aStatus, aBytes, aLength);
  }
}

GMPErr GMPRecordImpl::Write(const uint8_t* aData, uint32_t aDataSize) {
  // The IPDL actor might be destroyed on another thread, releasing the owner,
  // so take a strong reference to ensure it stays alive long enough.
  if (auto owner = GetOwner()) {
    return owner->Write(this, aData, aDataSize);
  }
  return GMPClosedErr;
}

void GMPRecordImpl::WriteComplete(GMPErr aStatus) {
  // While we know mClient is not yet destroyed if the pointer is still valid
  // while holding the lock, be careful to note that it may be destroyed by the
  // plugin after the callback returns.
  RecursiveMutexAutoLock lock(mMutex);
  if (mClient) {
    mClient->WriteComplete(aStatus);
  }
}

GMPErr GMPRecordImpl::Close() {
  RefPtr<GMPRecordImpl> kungfuDeathGrip(this);
  // Delete our self reference.
  Release();

  // We need to only clear mClient within the lock to avoid destroying it while
  // a callback is in progress. Since we have no control over the plugins on
  // when they choose to call Close, we must support this being re-entrant (e.g.
  // call Close from the OpenComplete, ReadComplete or WriteComplete callbacks).
  RefPtr<GMPStorageChild> owner;
  {
    RecursiveMutexAutoLock lock(mMutex);
    owner = RefPtr{mOwner};
    mClient = nullptr;
  }

  if (owner) {
    owner->Close(this->Name());
  }
  return GMPNoErr;
}

void GMPRecordImpl::DestroyOwner() {
  RecursiveMutexAutoLock lock(mMutex);
  mOwner = nullptr;
}

GMPStorageChild::GMPStorageChild(GMPChild* aPlugin) : mPlugin(aPlugin) {
  MOZ_ASSERT(ON_GMP_THREAD());
}

GMPErr GMPStorageChild::CreateRecord(const nsCString& aRecordName,
                                     GMPRecord** aOutRecord,
                                     GMPRecordClient* aClient) {
  MutexAutoLock lock(mMutex);

  if (NS_WARN_IF(mShutdown)) {
    return GMPClosedErr;
  }

  MOZ_ASSERT(!aRecordName.IsEmpty());
  MOZ_ASSERT(aOutRecord);

  if (HasRecord(aRecordName)) {
    return GMPRecordInUse;
  }

  auto record = MakeRefPtr<GMPRecordImpl>(this, aRecordName, aClient);
  mRecords.InsertOrUpdate(aRecordName, RefPtr{record});  // Addrefs

  // The GMPRecord holds a self reference until the GMP calls Close() on
  // it. This means the object is always valid (even if neutered) while
  // the GMP expects it to be.
  record.forget(aOutRecord);

  return GMPNoErr;
}

bool GMPStorageChild::HasRecord(const nsCString& aRecordName) {
  return mRecords.Contains(aRecordName);
}

already_AddRefed<GMPRecordImpl> GMPStorageChild::GetRecord(
    const nsCString& aRecordName) {
  MutexAutoLock lock(mMutex);
  if (NS_WARN_IF(mShutdown)) {
    return nullptr;
  }
  RefPtr<GMPRecordImpl> record;
  mRecords.Get(aRecordName, getter_AddRefs(record));
  return record.forget();
}

GMPErr GMPStorageChild::Open(GMPRecordImpl* aRecord) {
  MutexAutoLock lock(mMutex);

  if (NS_WARN_IF(mShutdown)) {
    return GMPClosedErr;
  }

  if (!HasRecord(aRecord->Name())) {
    // Trying to re-open a record that has already been closed.
    return GMPClosedErr;
  }

  CALL_ON_GMP_THREAD(SendOpen, aRecord->Name());

  return GMPNoErr;
}

GMPErr GMPStorageChild::Read(GMPRecordImpl* aRecord) {
  MutexAutoLock lock(mMutex);

  if (NS_WARN_IF(mShutdown)) {
    return GMPClosedErr;
  }

  if (!HasRecord(aRecord->Name())) {
    // Record not opened.
    return GMPClosedErr;
  }

  CALL_ON_GMP_THREAD(SendRead, aRecord->Name());

  return GMPNoErr;
}

GMPErr GMPStorageChild::Write(GMPRecordImpl* aRecord, const uint8_t* aData,
                              uint32_t aDataSize) {
  if (aDataSize > GMP_MAX_RECORD_SIZE) {
    return GMPQuotaExceededErr;
  }

  MutexAutoLock lock(mMutex);

  if (NS_WARN_IF(mShutdown)) {
    return GMPClosedErr;
  }

  if (!HasRecord(aRecord->Name())) {
    // Record not opened.
    return GMPClosedErr;
  }

  CALL_ON_GMP_THREAD(SendWrite, aRecord->Name(), ToArray(aData, aDataSize));

  return GMPNoErr;
}

GMPErr GMPStorageChild::Close(const nsCString& aRecordName) {
  MutexAutoLock lock(mMutex);

  if (!mRecords.Remove(aRecordName)) {
    // Already closed.
    return GMPClosedErr;
  }

  if (!mShutdown) {
    CALL_ON_GMP_THREAD(SendClose, aRecordName);
  }

  return GMPNoErr;
}

mozilla::ipc::IPCResult GMPStorageChild::RecvOpenComplete(
    const nsCString& aRecordName, const GMPErr& aStatus) {
  if (RefPtr<GMPRecordImpl> record = GetRecord(aRecordName)) {
    record->OpenComplete(aStatus);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPStorageChild::RecvReadComplete(
    const nsCString& aRecordName, const GMPErr& aStatus,
    nsTArray<uint8_t>&& aBytes) {
  if (RefPtr<GMPRecordImpl> record = GetRecord(aRecordName)) {
    record->ReadComplete(aStatus, aBytes.Elements(), aBytes.Length());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPStorageChild::RecvWriteComplete(
    const nsCString& aRecordName, const GMPErr& aStatus) {
  if (RefPtr<GMPRecordImpl> record = GetRecord(aRecordName)) {
    record->WriteComplete(aStatus);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPStorageChild::RecvShutdown() {
  // Block any new storage requests, and thus any messages back to the
  // parent. We don't delete any objects here, as that may invalidate
  // GMPRecord pointers held by the GMP.
  MutexAutoLock lock(mMutex);
  mShutdown = true;
  return IPC_OK();
}

void GMPStorageChild::ActorDestroy(ActorDestroyReason aWhy) {
  nsRefPtrHashtable<nsCStringHashKey, GMPRecordImpl> records;
  {
    MutexAutoLock lock(mMutex);
    mShutdown = true;
    records = std::move(mRecords);
  }

  // We know that any other thread that may be using the owner has a strong
  // reference to it, so it is safe to clear our own reference here.
  for (auto& record : records) {
    record.GetData()->DestroyOwner();
  }
}

}  // namespace mozilla::gmp

// avoid redefined macro in unified build
#undef ON_GMP_THREAD
#undef CALL_ON_GMP_THREAD
