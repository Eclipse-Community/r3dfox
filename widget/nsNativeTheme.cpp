/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeTheme.h"
#include "nsIWidget.h"
#include "mozilla/dom/Document.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsNameSpaceManager.h"
#include "nsStyleConsts.h"
#include "nsPIDOMWindow.h"
#include "nsProgressFrame.h"
#include "nsRangeFrame.h"
#include "nsCSSRendering.h"
#include "ImageContainer.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLProgressElement.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/DocumentInlines.h"
#include "nsXULElement.h"
#include <algorithm>

using namespace mozilla;
using namespace mozilla::dom;

nsNativeTheme::nsNativeTheme() : mAnimatedContentTimeout(UINT32_MAX) {}

NS_IMPL_ISUPPORTS(nsNativeTheme, nsITimerCallback, nsINamed)

static HTMLInputElement* GetContainingNumberInput(nsIContent* aContent) {
  auto* nacHost = aContent->GetClosestNativeAnonymousSubtreeRootParentOrHost();
  if (!nacHost) {
    return nullptr;
  }
  auto* input = HTMLInputElement::FromNode(nacHost);
  if (!input || input->ControlType() != FormControlType::InputNumber) {
    return nullptr;
  }
  return input;
}

/* static */ ElementState nsNativeTheme::GetContentState(
    nsIFrame* aFrame, StyleAppearance aAppearance) {
  if (!aFrame) {
    return ElementState();
  }

  nsIContent* frameContent = aFrame->GetContent();
  if (!frameContent || !frameContent->IsElement()) {
    return ElementState();
  }

  bool isXULElement = frameContent->IsXULElement();
  if (isXULElement &&
      (aAppearance == StyleAppearance::CheckboxLabel ||
       aAppearance == StyleAppearance::RadioLabel)) {
    aFrame = aFrame->GetParent()->GetParent();
    frameContent = aFrame->GetContent();
  }
  if (aAppearance == StyleAppearance::Checkbox ||
      aAppearance == StyleAppearance::Radio) {
    if (nsXULElement::FromNodeOrNull(frameContent->GetParent())) {
      aFrame = aFrame->GetParent();
      frameContent = frameContent->GetParent();
      isXULElement = true;
    }
  }
  MOZ_ASSERT(frameContent && frameContent->IsElement());

  ElementState flags = frameContent->AsElement()->StyleState();
  if (aAppearance == StyleAppearance::SpinnerDownbutton ||
      aAppearance == StyleAppearance::SpinnerUpbutton) {
    if (HTMLInputElement* numberInput = GetContainingNumberInput(frameContent);
        numberInput &&
        numberInput->StyleState().HasState(ElementState::DISABLED)) {
      flags |= ElementState::DISABLED;
    }
  }

  if (!isXULElement) {
    return flags;
  }

  switch (aAppearance) {
    case StyleAppearance::RadioLabel: {
      if (frameContent->AsElement()->HasAttr(nsGkAtoms::focused)) {
        flags |= ElementState::FOCUS;
        nsPIDOMWindowOuter* window =
            aFrame->GetContent()->OwnerDoc()->GetWindow();
        if (window && window->ShouldShowFocusRing()) {
          flags |= ElementState::FOCUSRING;
        }
      }
      break;
    }
    case StyleAppearance::CheckboxLabel: {
      if (!flags.HasState(ElementState::CHECKED) &&
          CheckBooleanAttr(aFrame, nsGkAtoms::indeterminate)) {
        flags |= ElementState::INDETERMINATE;
      }
      break;
    }
    case StyleAppearance::Toolbarbutton:
      if (CheckBooleanAttr(aFrame, nsGkAtoms::open)) {
        // TODO: Consider using :open instead of faking :hover:active?
        flags |= ElementState::HOVER | ElementState::ACTIVE;
      }
      break;
    default:
      break;
  }

  return flags;
}

/* static */
bool nsNativeTheme::CheckBooleanAttr(nsIFrame* aFrame, nsAtom* aAtom) {
  if (!aFrame) return false;

  nsIContent* content = aFrame->GetContent();
  if (!content || !content->IsElement()) return false;

  if (content->IsHTMLElement()) return content->AsElement()->HasAttr(aAtom);

  // For XML/XUL elements, an attribute must be equal to the literal
  // string "true" to be counted as true.  An empty string should _not_
  // be counted as true.
  return content->AsElement()->AttrValueIs(kNameSpaceID_None, aAtom, u"true"_ns,
                                           eCaseMatters);
}

/* static */
double nsNativeTheme::GetProgressValue(nsIFrame* aFrame) {
  if (!aFrame || !aFrame->GetContent()->IsHTMLElement(nsGkAtoms::progress)) {
    return 0;
  }

  return static_cast<HTMLProgressElement*>(aFrame->GetContent())->Value();
}

/* static */
double nsNativeTheme::GetProgressMaxValue(nsIFrame* aFrame) {
  if (!aFrame || !aFrame->GetContent()->IsHTMLElement(nsGkAtoms::progress)) {
    return 100;
  }

  return static_cast<HTMLProgressElement*>(aFrame->GetContent())->Max();
}

bool nsNativeTheme::IsButtonTypeMenu(nsIFrame* aFrame) {
  if (!aFrame) return false;

  nsIContent* content = aFrame->GetContent();
  return content->IsXULElement(nsGkAtoms::button) &&
         content->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                                           u"menu"_ns, eCaseMatters);
}

bool nsNativeTheme::IsWidgetStyled(nsPresContext* aPresContext,
                                   nsIFrame* aFrame,
                                   StyleAppearance aAppearance) {
  // Check for specific widgets to see if HTML has overridden the style.
  if (!aFrame) {
    return false;
  }

  /**
   * Progress bar appearance should be the same for the bar and the container
   * frame.
   */
  if (aAppearance == StyleAppearance::ProgressBar ||
      aAppearance == StyleAppearance::Meter) {
    if (nsProgressFrame* progressFrame = do_QueryFrame(aFrame)) {
      return !progressFrame->ShouldUseNativeStyle();
    }
  }

  /**
   * An nsRangeFrame and its children are treated atomically when it
   * comes to native theming (either all parts, or no parts, are themed).
   * nsRangeFrame owns the logic and will tell us what we should do.
   */
  if (aAppearance == StyleAppearance::Range) {
    if (nsRangeFrame* rangeFrame = do_QueryFrame(aFrame)) {
      return !rangeFrame->ShouldUseNativeStyle();
    }
  }

  return nsLayoutUtils::AuthorSpecifiedBorderBackgroundDisablesTheming(
             aAppearance) &&
         aFrame->GetContent()->IsHTMLElement() &&
         aFrame->Style()->HasAuthorSpecifiedBorderOrBackground();
}

/* static */
bool nsNativeTheme::IsFrameRTL(nsIFrame* aFrame) {
  if (!aFrame) {
    return false;
  }
  return aFrame->GetWritingMode().IsPhysicalRTL();
}

/* static */
bool nsNativeTheme::IsHTMLContent(nsIFrame* aFrame) {
  if (!aFrame) {
    return false;
  }
  nsIContent* content = aFrame->GetContent();
  return content && content->IsHTMLElement();
}

bool nsNativeTheme::IsVerticalProgress(nsIFrame* aFrame) {
  if (!aFrame) {
    return false;
  }
  return IsVerticalMeter(aFrame);
}

bool nsNativeTheme::IsVerticalMeter(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "You have to pass a non-null aFrame");
  switch (aFrame->StyleDisplay()->mOrient) {
    case StyleOrient::Horizontal:
      return false;
    case StyleOrient::Vertical:
      return true;
    case StyleOrient::Inline:
      return aFrame->GetWritingMode().IsVertical();
    case StyleOrient::Block:
      return !aFrame->GetWritingMode().IsVertical();
  }
  MOZ_ASSERT_UNREACHABLE("unexpected -moz-orient value");
  return false;
}

bool nsNativeTheme::QueueAnimatedContentForRefresh(nsIContent* aContent,
                                                   uint32_t aMinimumFrameRate) {
  NS_ASSERTION(aContent, "Null pointer!");
  NS_ASSERTION(aMinimumFrameRate, "aMinimumFrameRate must be non-zero!");
  NS_ASSERTION(aMinimumFrameRate <= 1000,
               "aMinimumFrameRate must be less than 1000!");

  uint32_t timeout = 1000 / aMinimumFrameRate;
  timeout = std::min(mAnimatedContentTimeout, timeout);

  if (!mAnimatedContentTimer) {
    mAnimatedContentTimer = NS_NewTimer();
    NS_ENSURE_TRUE(mAnimatedContentTimer, false);
  }

  if (mAnimatedContentList.IsEmpty() || timeout != mAnimatedContentTimeout) {
    nsresult rv;
    if (!mAnimatedContentList.IsEmpty()) {
      rv = mAnimatedContentTimer->Cancel();
      NS_ENSURE_SUCCESS(rv, false);
    }

    if (XRE_IsContentProcess() && NS_IsMainThread()) {
      mAnimatedContentTimer->SetTarget(GetMainThreadSerialEventTarget());
    }
    rv = mAnimatedContentTimer->InitWithCallback(this, timeout,
                                                 nsITimer::TYPE_ONE_SHOT);
    NS_ENSURE_SUCCESS(rv, false);

    mAnimatedContentTimeout = timeout;
  }

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  mAnimatedContentList.AppendElement(aContent);

  return true;
}

NS_IMETHODIMP
nsNativeTheme::Notify(nsITimer* aTimer) {
  NS_ASSERTION(aTimer == mAnimatedContentTimer, "Wrong timer!");

  // XXX Assumes that calling nsIFrame::Invalidate won't reenter
  //     QueueAnimatedContentForRefresh.

  uint32_t count = mAnimatedContentList.Length();
  for (uint32_t index = 0; index < count; index++) {
    nsIFrame* frame = mAnimatedContentList[index]->GetPrimaryFrame();
    if (frame) {
      frame->InvalidateFrame();
    }
  }

  mAnimatedContentList.Clear();
  mAnimatedContentTimeout = UINT32_MAX;
  return NS_OK;
}

NS_IMETHODIMP
nsNativeTheme::GetName(nsACString& aName) {
  aName.AssignLiteral("nsNativeTheme");
  return NS_OK;
}

nsIFrame* nsNativeTheme::GetAdjacentSiblingFrameWithSameAppearance(
    nsIFrame* aFrame, bool aNextSibling) {
  if (!aFrame) return nullptr;

  // Find the next visible sibling.
  nsIFrame* sibling = aFrame;
  do {
    sibling =
        aNextSibling ? sibling->GetNextSibling() : sibling->GetPrevSibling();
  } while (sibling && sibling->GetRect().Width() == 0);

  // Check same appearance and adjacency.
  if (!sibling ||
      sibling->StyleDisplay()->EffectiveAppearance() !=
          aFrame->StyleDisplay()->EffectiveAppearance() ||
      (sibling->GetRect().XMost() != aFrame->GetRect().X() &&
       aFrame->GetRect().XMost() != sibling->GetRect().X()))
    return nullptr;
  return sibling;
}

bool nsNativeTheme::IsRangeHorizontal(nsIFrame* aFrame) {
  nsIFrame* rangeFrame = aFrame;
  if (!rangeFrame->IsRangeFrame()) {
    // If the thumb's frame is passed in, get its range parent:
    rangeFrame = aFrame->GetParent();
  }
  if (rangeFrame->IsRangeFrame()) {
    return static_cast<nsRangeFrame*>(rangeFrame)->IsHorizontal();
  }
  // Not actually a range frame - just use the ratio of the frame's size to
  // decide:
  return aFrame->GetSize().width >= aFrame->GetSize().height;
}

/* static */
bool nsNativeTheme::IsDarkBackgroundForScrollbar(nsIFrame* aFrame) {
  // Try to find the scrolled frame. Note that for stuff like xul <tree> there
  // might be none.
  {
    nsIFrame* frame = aFrame;
    ScrollContainerFrame* scrollContainerFrame = nullptr;
    while (!scrollContainerFrame && frame) {
      scrollContainerFrame = frame->GetScrollTargetFrame();
      frame = frame->GetParent();
    }
    if (scrollContainerFrame) {
      aFrame = scrollContainerFrame->GetScrolledFrame();
    } else {
      // Leave aFrame untouched.
    }
  }

  return IsDarkBackground(aFrame);
}

/* static */
bool nsNativeTheme::IsDarkBackground(nsIFrame* aFrame) {
  auto color =
      nsCSSRendering::FindEffectiveBackgroundColor(
          aFrame, /* aStopAtThemed = */ false, /* aPreferBodyToCanvas = */ true)
          .mColor;
  return LookAndFeel::IsDarkColor(color);
}

/*static*/
bool nsNativeTheme::IsWidgetScrollbarPart(StyleAppearance aAppearance) {
  switch (aAppearance) {
    case StyleAppearance::ScrollbarVertical:
    case StyleAppearance::ScrollbarHorizontal:
    case StyleAppearance::ScrollbarbuttonUp:
    case StyleAppearance::ScrollbarbuttonDown:
    case StyleAppearance::ScrollbarbuttonLeft:
    case StyleAppearance::ScrollbarbuttonRight:
    case StyleAppearance::ScrollbarthumbVertical:
    case StyleAppearance::ScrollbarthumbHorizontal:
    case StyleAppearance::Scrollcorner:
      return true;
    default:
      return false;
  }
}

/*static*/
bool nsNativeTheme::IsWidgetAlwaysNonNative(nsIFrame* aFrame,
                                            StyleAppearance aAppearance) {
  return IsWidgetScrollbarPart(aAppearance) ||
         aAppearance == StyleAppearance::FocusOutline ||
         aAppearance == StyleAppearance::SpinnerUpbutton ||
         aAppearance == StyleAppearance::SpinnerDownbutton ||
         aAppearance == StyleAppearance::Toolbarbutton ||
         aAppearance == StyleAppearance::ProgressBar ||
         aAppearance == StyleAppearance::Meter ||
         aAppearance == StyleAppearance::Range ||
         aAppearance == StyleAppearance::Listbox;
}
