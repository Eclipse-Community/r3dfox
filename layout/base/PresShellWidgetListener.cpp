/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PresShellWidgetListener.h"

#include "WindowRenderer.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/widget/Screen.h"
#include "nsContentUtils.h"  // for nsAutoScriptBlocker
#include "nsDeviceContext.h"
#include "nsDocShell.h"
#include "nsIFrame.h"
#include "nsIWidget.h"
#include "nsIWidgetListener.h"
#include "nsLayoutUtils.h"
#include "nsXULPopupManager.h"

using namespace mozilla;
using namespace mozilla::widget;

static uint32_t gLastUserEventTime = 0;
static void MaybeUpdateLastUserEventTime(WidgetGUIEvent* aEvent) {
  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  if ((mouseEvent &&
       // Ignore mouse events that we synthesize.
       mouseEvent->mReason == WidgetMouseEvent::eReal &&
       // Ignore mouse exit and enter (we'll get moves if the user
       // is really moving the mouse) since we get them when we
       // create and destroy widgets.
       mouseEvent->mMessage != eMouseExitFromWidget &&
       mouseEvent->mMessage != eMouseEnterIntoWidget) ||
      aEvent->HasKeyEventMessage() || aEvent->HasIMEEventMessage()) {
    gLastUserEventTime = PR_IntervalToMicroseconds(PR_IntervalNow());
  }
}

uint32_t PresShellWidgetListener::GetLastUserEventTime() {
  return gLastUserEventTime;
}

PresShellWidgetListener::PresShellWidgetListener(PresShell* aPs)
    : mPresShell(aPs) {
  MOZ_COUNT_CTOR(PresShellWidgetListener);
}

PresShellWidgetListener::~PresShellWidgetListener() {
  MOZ_COUNT_DTOR(PresShellWidgetListener);

  if (mPreviousWindow) {
    mPreviousWindow->SetPreviouslyAttachedWidgetListener(nullptr);
  }

  // Destroy and release the widget
  DetachWidget();
}

void PresShellWidgetListener::DetachWidget() {
  if (mWindow) {
    mWindow->SetAttachedWidgetListener(nullptr);
    mWindow = nullptr;
  }
}

// Attach to a top level widget and start receiving mirrored events.
void PresShellWidgetListener::AttachToTopLevelWidget(nsIWidget* aWidget) {
  MOZ_ASSERT(aWidget, "null widget ptr");
  MOZ_ASSERT(!aWidget->GetWidgetListener() ||
                 aWidget->GetWidgetListener()->GetAppWindow(),
             "Expect a top level widget");

  /// XXXjimm This is a temporary workaround to an issue w/document
  // viewer (bug 513162).
  if (nsIWidgetListener* listener = aWidget->GetAttachedWidgetListener()) {
    if (auto* old = listener->GetAsPresShellWidgetListener()) {
      old->DetachFromTopLevelWidget();
    }
  }

  mWindow = aWidget;

  mWindow->SetAttachedWidgetListener(this);
  if (mWindow->GetWindowType() != WindowType::Invisible) {
    mWindow->AsyncEnableDragDrop(true);
  }
}

// Detach us from an attached widget.
void PresShellWidgetListener::DetachFromTopLevelWidget() {
  MOZ_ASSERT(mWindow, "null mWindow for DetachFromTopLevelWidget!");

  mWindow->SetAttachedWidgetListener(nullptr);
  if (nsIWidgetListener* listener =
          mWindow->GetPreviouslyAttachedWidgetListener()) {
    if (auto* previousListener = listener->GetAsPresShellWidgetListener()) {
      // Ensure the listener doesn't think it's being used anymore
      previousListener->mPreviousWindow = nullptr;
    }
  }

  // If the new pres shell is paint suppressed then the window
  // will want to use us instead until that's done
  mWindow->SetPreviouslyAttachedWidgetListener(this);
  mPreviousWindow = std::move(mWindow);
  MOZ_ASSERT(!mWindow);
}

PresShell* PresShellWidgetListener::GetPresShell() { return mPresShell; }

bool PresShellWidgetListener::WindowResized(nsIWidget* aWidget, int32_t aWidth,
                                            int32_t aHeight) {
  RefPtr<PresShell> ps = GetPresShell();
  if (!ps) {
    return false;
  }

  nsPresContext* pc = ps->GetPresContext();
  if (!pc) {
    return false;
  }

  // ensure DPI is up-to-date, in case of window being opened and sized
  // on a non-default-dpi display (bug 829963)
  pc->DeviceContext()->CheckDPIChange();
  int32_t p2a = pc->AppUnitsPerDevPixel();
  if (auto* frame = ps->GetRootFrame()) {
    // Usually the resize would deal with this, but there are some cases (like
    // web-extension popups) where frames might already be correctly sized etc
    // due to a call to e.g. nsDocumentViewer::GetContentSize or so.
    frame->InvalidateFrame();
  }
  const LayoutDeviceIntSize size(aWidth, aHeight);
  ps->SetLayoutViewportSize(LayoutDeviceIntSize::ToAppUnits(size, p2a),
                            /* aDelay = */ false);

  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    pm->AdjustPopupsOnWindowChange(ps);
  }

  return true;
}

void PresShellWidgetListener::DynamicToolbarMaxHeightChanged(ScreenIntCoord aHeight) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
  CallOnAllRemoteChildren(
      [aHeight](dom::BrowserParent* aBrowserParent) -> CallState {
        aBrowserParent->DynamicToolbarMaxHeightChanged(aHeight);
        return CallState::Continue;
      });
}

void PresShellWidgetListener::DynamicToolbarOffsetChanged(ScreenIntCoord aOffset) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
  CallOnAllRemoteChildren(
      [aOffset](dom::BrowserParent* aBrowserParent) -> CallState {
        // Skip background tabs.
        if (!aBrowserParent->GetDocShellIsActive()) {
          return CallState::Continue;
        }

        aBrowserParent->DynamicToolbarOffsetChanged(aOffset);
        return CallState::Stop;
      });
}

void PresShellWidgetListener::KeyboardHeightChanged(ScreenIntCoord aHeight) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
#ifdef MOZ_WIDGET_ANDROID
  CallOnAllRemoteChildren(
      [aHeight](dom::BrowserParent* aBrowserParent) -> CallState {
        // Skip background tabs.
        if (!aBrowserParent->GetDocShellIsActive()) {
          return CallState::Continue;
        }

        aBrowserParent->KeyboardHeightChanged(aHeight);
        return CallState::Stop;
      });
#endif
}

void PresShellWidgetListener::AndroidPipModeChanged(bool aPipMode) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Should be only called for the browser parent process");
#ifdef MOZ_WIDGET_ANDROID
  CallOnAllRemoteChildren(
      [aPipMode](dom::BrowserParent* aBrowserParent) -> CallState {
        aBrowserParent->AndroidPipModeChanged(aPipMode);
        return CallState::Continue;
      });
#endif
}

void PresShellWidgetListener::WillPaintWindow(nsIWidget* aWidget) {
  WindowRenderer* renderer = aWidget->GetWindowRenderer();
  if (renderer->NeedsWidgetInvalidation()) {
    return;
  }
  if (RefPtr ps = mPresShell) {
    // FIXME(bug 2002232): Why is this needed?
    ps->PaintSynchronously();
  }
}

bool PresShellWidgetListener::PaintWindow(nsIWidget* aWidget, LayoutDeviceIntRegion) {
  RefPtr ps = mPresShell;
  if (!ps) {
    return true;
  }
  RefPtr renderer = aWidget->GetWindowRenderer();
  if (!renderer->NeedsWidgetInvalidation()) {
    renderer->FlushRendering(wr::RenderReasons::WIDGET);
  } else {
    ps->SyncPaintFallback(ps->GetRootFrame(), renderer);
  }
  mozilla::StartupTimeline::RecordOnce(mozilla::StartupTimeline::FIRST_PAINT);
  return true;
}

void PresShellWidgetListener::DidPaintWindow() {
  if (RefPtr<PresShell> presShell = mPresShell) {
    presShell->DidPaintWindow();
  }
}

void PresShellWidgetListener::DidCompositeWindow(
    mozilla::layers::TransactionId aTransactionId,
    const TimeStamp& aCompositeStart, const TimeStamp& aCompositeEnd) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  nsAutoScriptBlocker scriptBlocker;

  nsPresContext* context = presShell->GetPresContext();
  nsRootPresContext* rootContext = context->GetRootPresContext();
  if (rootContext) {
    rootContext->NotifyDidPaintForSubtree(aTransactionId, aCompositeEnd);
  }

  mozilla::StartupTimeline::RecordOnce(mozilla::StartupTimeline::FIRST_PAINT2,
                                       aCompositeEnd);
}

nsEventStatus PresShellWidgetListener::HandleEvent(WidgetGUIEvent* aEvent) {
  MOZ_ASSERT(aEvent->mWidget, "null widget ptr");

  nsEventStatus result = nsEventStatus_eIgnore;
  MaybeUpdateLastUserEventTime(aEvent);
  if (RefPtr<PresShell> ps = GetPresShell()) {
    if (nsIFrame* root = ps->GetRootFrame()) {
      ps->HandleEvent(root, aEvent, false, &result);
    }
  }
  return result;
}

void PresShellWidgetListener::SafeAreaInsetsChanged(
    const LayoutDeviceIntMargin& aSafeAreaInsets) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  LayoutDeviceIntMargin windowSafeAreaInsets;
  const LayoutDeviceIntRect windowRect = mWindow->GetScreenBounds();
  if (nsCOMPtr<nsIScreen> screen = mWindow->GetWidgetScreen()) {
    windowSafeAreaInsets = nsContentUtils::GetWindowSafeAreaInsets(
        screen, aSafeAreaInsets, windowRect);
  }

  presShell->GetPresContext()->SetSafeAreaInsets(windowSafeAreaInsets);

  // https://github.com/w3c/csswg-drafts/issues/4670
  // Actually we don't set this value on sub document. This behaviour is
  // same as Blink.
  CallOnAllRemoteChildren(
      [windowSafeAreaInsets](dom::BrowserParent* aBrowserParent) -> CallState {
        (void)aBrowserParent->SendSafeAreaInsetsChanged(windowSafeAreaInsets);
        return CallState::Continue;
      });
}

bool PresShellWidgetListener::IsPrimaryFramePaintSuppressed() const {
  return StaticPrefs::layout_show_previous_page() && mPresShell &&
         mPresShell->IsPaintingSuppressed();
}

void PresShellWidgetListener::CallOnAllRemoteChildren(
    const std::function<CallState(dom::BrowserParent*)>& aCallback) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  dom::Document* document = presShell->GetDocument();
  if (!document) {
    return;
  }

  nsPIDOMWindowOuter* window = document->GetWindow();
  if (!window) {
    return;
  }

  nsContentUtils::CallOnAllRemoteChildren(window, aCallback);
}
