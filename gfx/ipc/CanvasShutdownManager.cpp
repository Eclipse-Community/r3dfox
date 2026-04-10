/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasShutdownManager.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/layers/PersistentBufferProvider.h"

using namespace mozilla::dom;

namespace mozilla::gfx {

StaticMutex CanvasShutdownManager::sManagersMutex;
MOZ_RUNINIT std::set<CanvasShutdownManager*> CanvasShutdownManager::sManagers;

// The owning thread will tell us to close when it is shutdown, either via
// CanvasShutdownManager::Shutdown for the main thread, or via a shutdown
// callback from ThreadSafeWorkerRef for worker threads.
MOZ_THREAD_LOCAL(CanvasShutdownManager*) CanvasShutdownManager::sLocalManager;

CanvasShutdownManager::CanvasShutdownManager(StrongWorkerRef* aWorkerRef)
    : mWorkerRef(new ThreadSafeWorkerRef(aWorkerRef)) {}

CanvasShutdownManager::CanvasShutdownManager() = default;
CanvasShutdownManager::~CanvasShutdownManager() = default;

std::vector<RefPtr<CanvasRenderingContext2D>>
CanvasShutdownManager::RefActiveCanvas() const {
  std::vector<RefPtr<CanvasRenderingContext2D>> activeCanvas;
  activeCanvas.reserve(mActiveCanvas.size());
  for (const auto& canvas : mActiveCanvas) {
    activeCanvas.emplace_back(canvas);
  }
  return activeCanvas;
}

void CanvasShutdownManager::Destroy() {
  auto activeCanvas = RefActiveCanvas();
  mActiveCanvas.clear();
  for (const auto& canvas : activeCanvas) {
    canvas->OnShutdown();
  }

  CanvasManagerChild::Shutdown();
  mWorkerRef = nullptr;
}

/* static */ void CanvasShutdownManager::Shutdown() {
  auto* manager = MaybeGet();
  if (!manager) {
    return;
  }

  {
    StaticMutexAutoLock lock(sManagersMutex);
    sManagers.erase(manager);
  }

  sLocalManager.set(nullptr);
  manager->Destroy();
  delete manager;
}

/* static */ CanvasShutdownManager* CanvasShutdownManager::MaybeGet() {
  if (NS_WARN_IF(!sLocalManager.init())) {
    return nullptr;
  }

  return sLocalManager.get();
}

/* static */ CanvasShutdownManager* CanvasShutdownManager::Get() {
  if (NS_WARN_IF(!sLocalManager.init())) {
    return nullptr;
  }

  CanvasShutdownManager* managerWeak = sLocalManager.get();
  if (managerWeak) {
    return managerWeak;
  }

  if (WorkerPrivate* worker = GetCurrentThreadWorkerPrivate()) {
    // The ThreadSafeWorkerRef will let us know when the worker is shutting
    // down. This will let us clear our threadlocal reference and close the
    // actor. We rely upon an explicit shutdown for the main thread.
    RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
        worker, "CanvasShutdownManager", []() { Shutdown(); });
    if (NS_WARN_IF(!workerRef)) {
      return nullptr;
    }

    CanvasShutdownManager* manager = new CanvasShutdownManager(workerRef);
    sLocalManager.set(manager);

    StaticMutexAutoLock lock(sManagersMutex);
    sManagers.insert(manager);
    return manager;
  }

  if (NS_IsMainThread()) {
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      return nullptr;
    }

    CanvasShutdownManager* manager = new CanvasShutdownManager();
    sLocalManager.set(manager);

    StaticMutexAutoLock lock(sManagersMutex);
    sManagers.insert(manager);
    return manager;
  }

  MOZ_ASSERT_UNREACHABLE("Can only be used on main or DOM worker threads!");
  return nullptr;
}

void CanvasShutdownManager::AddShutdownObserver(
    dom::CanvasRenderingContext2D* aCanvas) {
  mActiveCanvas.insert(aCanvas);
}

void CanvasShutdownManager::RemoveShutdownObserver(
    dom::CanvasRenderingContext2D* aCanvas) {
  mActiveCanvas.erase(aCanvas);
}

void CanvasShutdownManager::OnRemoteCanvasLost() {
  for (const auto& canvas : RefActiveCanvas()) {
    canvas->OnRemoteCanvasLost();
  }
}

void CanvasShutdownManager::OnRemoteCanvasRestored() {
  for (const auto& canvas : RefActiveCanvas()) {
    canvas->OnRemoteCanvasRestored();
  }
}

void CanvasShutdownManager::OnRemoteCanvasReset(
    const nsTArray<layers::RemoteTextureOwnerId>& aOwnerIds) {
  if (aOwnerIds.IsEmpty()) {
    return;
  }

  for (const auto& canvas : RefActiveCanvas()) {
    auto* bufferProvider = canvas->GetBufferProvider();
    if (!bufferProvider) {
      continue;
    }

    Maybe<layers::RemoteTextureOwnerId> ownerId =
        bufferProvider->GetRemoteTextureOwnerId();
    if (!ownerId) {
      continue;
    }

    if (aOwnerIds.Contains(*ownerId)) {
      canvas->OnRemoteCanvasLost();
      canvas->OnRemoteCanvasRestored();
    }
  }
}

/* static */ void CanvasShutdownManager::MaybeRestoreRemoteCanvas() {
  // Calling Get will recreate the CanvasManagerChild, which in turn will
  // cause us to call OnRemoteCanvasRestore upon success.
  if (CanvasShutdownManager* manager = MaybeGet()) {
    if (!manager->mActiveCanvas.empty()) {
      CanvasManagerChild::Get();
    }
  }
}

/* static */ void CanvasShutdownManager::OnCompositorManagerRestored() {
  MOZ_ASSERT(NS_IsMainThread());

  class RestoreRunnable final : public MainThreadWorkerRunnable {
   public:
    RestoreRunnable()
        : MainThreadWorkerRunnable("CanvasShutdownManager::RestoreRunnable") {}

    bool WorkerRun(JSContext*, WorkerPrivate*) override {
      MaybeRestoreRemoteCanvas();
      return true;
    }
  };

  // We can restore the main thread canvases immediately.
  MaybeRestoreRemoteCanvas();

  // And dispatch to restore any DOM worker canvases. This is safe because we
  // remove the manager from sManagers before clearing mWorkerRef during DOM
  // worker shutdown.
  StaticMutexAutoLock lock(sManagersMutex);
  for (const auto& manager : sManagers) {
    if (manager->mWorkerRef) {
      auto task = MakeRefPtr<RestoreRunnable>();
      task->Dispatch(manager->mWorkerRef->Private());
    }
  }
}

}  // namespace mozilla::gfx
