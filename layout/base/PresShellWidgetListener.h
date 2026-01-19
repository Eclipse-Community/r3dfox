/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_PresShellWidgetListener_h__
#define mozilla_PresShellWidgetListener_h__

#include "Units.h"
#include "mozilla/Attributes.h"
#include "mozilla/CallState.h"
#include "nsCOMPtr.h"
#include "nsIWidgetListener.h"

class nsIWidget;

namespace mozilla {
class PresShell;
namespace dom {
class BrowserParent;
}  // namespace dom
namespace widget {
struct InitData;
enum class TransparencyMode : uint8_t;
enum class WindowType : uint8_t;
}  // namespace widget

class PresShellWidgetListener final : public nsIWidgetListener {
 public:
  // Stops listening to mWidget and clears it.
  void DetachWidget();

  /**
   * Attach/detach a top level widget. Attaching allows the shell to begin
   * receiving gecko events. The underlying base window associated with the
   * widget will continues to receive events it expects.
   *
   * An attached widget will not be destroyed when we are destroyed,
   * allowing the recycling of a single top level widget over multiple pres
   * shells.
   *
   * @param aWidget The widget to attach to / detach from.
   */
  void AttachToTopLevelWidget(nsIWidget* aWidget);
  void DetachFromTopLevelWidget();

  static uint32_t GetLastUserEventTime();

  nsIWidget* GetWidget() const { return mWindow; }
  bool HasWidget() const { return !!mWindow; }

  // nsIWidgetListener
  mozilla::PresShell* GetPresShell() override;
  PresShellWidgetListener* GetAsPresShellWidgetListener() override {
    return this;
  }
  bool IsPaintSuppressed() const override {
    return IsPrimaryFramePaintSuppressed();
  }
  bool WindowResized(nsIWidget* aWidget, int32_t aWidth,
                     int32_t aHeight) override;
  void DynamicToolbarMaxHeightChanged(mozilla::ScreenIntCoord aHeight) override;
  void DynamicToolbarOffsetChanged(mozilla::ScreenIntCoord aOffset) override;
  void KeyboardHeightChanged(mozilla::ScreenIntCoord aHeight) override;
  void AndroidPipModeChanged(bool) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void WillPaintWindow(nsIWidget* aWidget) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  bool PaintWindow(nsIWidget* aWidget, LayoutDeviceIntRegion aRegion) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void DidPaintWindow() override;
  void DidCompositeWindow(mozilla::layers::TransactionId aTransactionId,
                          const mozilla::TimeStamp& aCompositeStart,
                          const mozilla::TimeStamp& aCompositeEnd) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsEventStatus HandleEvent(mozilla::WidgetGUIEvent*) override;
  void SafeAreaInsetsChanged(const mozilla::LayoutDeviceIntMargin&) override;

  explicit PresShellWidgetListener(mozilla::PresShell*);
  virtual ~PresShellWidgetListener();

  bool IsPrimaryFramePaintSuppressed() const;

 private:
  void CallOnAllRemoteChildren(
      const std::function<mozilla::CallState(mozilla::dom::BrowserParent*)>&
          aCallback);

  mozilla::PresShell* mPresShell = nullptr;
  nsCOMPtr<nsIWidget> mWindow;
  nsCOMPtr<nsIWidget> mPreviousWindow;
};

}  // namespace mozilla

#endif
