/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ClipManager.h"

#include "DisplayItemClipChain.h"
#include "FrameMetrics.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/dom/Document.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"
#include "nsRefreshDriver.h"
#include "nsStyleStructInlines.h"
#include "UnitTransforms.h"

static mozilla::LazyLogModule sClipLog("wr.clip");
#define CLIP_LOG(...) MOZ_LOG(sClipLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla::layers {

ClipManager::ClipManager() : mManager(nullptr), mBuilder(nullptr) {}

void ClipManager::PushCacheScope() {
  if (mCacheStackTop < mCacheStack.size()) {
    mCacheStack[mCacheStackTop].clear();
  } else {
    mCacheStack.emplace_back();
  }
  mCacheStackTop++;
}

void ClipManager::PopCacheScope() {
  MOZ_ASSERT(mCacheStackTop > 0);
  mCacheStackTop--;
}

void ClipManager::BeginBuild(WebRenderLayerManager* aManager,
                             wr::DisplayListBuilder& aBuilder) {
  MOZ_ASSERT(!mManager);
  mManager = aManager;
  MOZ_ASSERT(!mBuilder);
  mBuilder = &aBuilder;
  MOZ_ASSERT(mCacheStackTop == 0);
  PushCacheScope();
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void ClipManager::EndBuild() {
  mBuilder = nullptr;
  mManager = nullptr;
  PopCacheScope();
  MOZ_ASSERT(mCacheStackTop == 0);
  MOZ_ASSERT(mASROverride.empty());
  MOZ_ASSERT(mItemClipStack.empty());
}

void ClipManager::BeginList(const StackingContextHelper& aStackingContext) {
  CLIP_LOG("begin list %p affects = %d, ref-frame = %d\n", &aStackingContext,
           aStackingContext.AffectsClipPositioning(),
           aStackingContext.ReferenceFrameId().isSome());

  ItemClips clips(nullptr, nullptr, 0, false);
  if (!mItemClipStack.empty()) {
    clips = mItemClipStack.top();
  }

  if (aStackingContext.AffectsClipPositioning()) {
    if (auto referenceFrameId = aStackingContext.ReferenceFrameId()) {
      PushOverrideForASR(clips.mASR, *referenceFrameId);
      clips.mScrollId = *referenceFrameId;
    } else {
      // Start a new cache
      PushCacheScope();
    }
    // Ensure we recreate the chain id if needed.
    clips.mClipChainId.reset();
  }

  CLIP_LOG("  push: clip: %p, asr: %p, scroll =%" PRIuPTR ", clip =%" PRIu64
           "\n",
           clips.mChain, clips.mASR, clips.mScrollId.id,
           clips.mClipChainId.valueOr(wr::WrClipChainId{0}).id);

  mItemClipStack.push(clips);
}

void ClipManager::EndList(const StackingContextHelper& aStackingContext) {
  MOZ_ASSERT(!mItemClipStack.empty());

  CLIP_LOG("end list %p\n", &aStackingContext);

  mBuilder->SetClipChainLeaf(Nothing());
  mItemClipStack.pop();

  if (aStackingContext.AffectsClipPositioning()) {
    if (aStackingContext.ReferenceFrameId()) {
      PopOverrideForASR(mItemClipStack.empty() ? nullptr
                                               : mItemClipStack.top().mASR);
    } else {
      PopCacheScope();
    }
  }
}

void ClipManager::PushOverrideForASR(const ActiveScrolledRoot* aASR,
                                     const wr::WrSpatialId& aSpatialId) {
  wr::WrSpatialId space = GetSpatialId(aASR);

  CLIP_LOG("Pushing %p override %zu -> %zu\n", aASR, space.id, aSpatialId.id);
  auto it = mASROverride.insert({space, std::stack<wr::WrSpatialId>()});
  it.first->second.push(aSpatialId);

  // Start a new cache
  PushCacheScope();

  // Fix up our cached item clip if needed.
  if (!mItemClipStack.empty()) {
    auto& top = mItemClipStack.top();
    if (top.mASR == aASR) {
      top.mScrollId = aSpatialId;
      top.mClipChainId.reset();
    }
  }
}

void ClipManager::PopOverrideForASR(const ActiveScrolledRoot* aASR) {
  PopCacheScope();

  wr::WrSpatialId space = GetSpatialId(aASR);
  auto it = mASROverride.find(space);
  if (it == mASROverride.end()) {
    MOZ_ASSERT_UNREACHABLE("Push/PopOverrideForASR should be balanced");
  } else {
    CLIP_LOG("Popping %p override %zu -> %zu\n", aASR, space.id,
             it->second.top().id);
    it->second.pop();
  }

  if (!mItemClipStack.empty()) {
    auto& top = mItemClipStack.top();
    if (top.mASR == aASR) {
      top.mScrollId = (it == mASROverride.end() || it->second.empty())
                          ? space
                          : it->second.top();
      top.mClipChainId.reset();
    }
  }

  if (it != mASROverride.end() && it->second.empty()) {
    mASROverride.erase(it);
  }
}

wr::WrSpatialId ClipManager::SpatialIdAfterOverride(
    const wr::WrSpatialId& aSpatialId) {
  auto it = mASROverride.find(aSpatialId);
  if (it == mASROverride.end()) {
    return aSpatialId;
  }
  MOZ_ASSERT(!it->second.empty());
  CLIP_LOG("Overriding %zu with %zu\n", aSpatialId.id, it->second.top().id);
  return it->second.top();
}

wr::WrSpaceAndClipChain ClipManager::SwitchItem(nsDisplayListBuilder* aBuilder,
                                                nsDisplayItem* aItem) {
  const DisplayItemClipChain* clip = aItem->GetClipChain();
  const DisplayItemClipChain* inheritedClipChain =
      mBuilder->GetInheritedClipChain();
  if (inheritedClipChain && inheritedClipChain != clip) {
    if (!clip) {
      clip = mBuilder->GetInheritedClipChain();
    } else {
      clip = aBuilder->CreateClipChainIntersection(
          mBuilder->GetInheritedClipChain(), clip);
    }
  }
  const ActiveScrolledRoot* asr = aItem->GetActiveScrolledRoot();
  DisplayItemType type = aItem->GetType();
  const ActiveScrolledRoot* stickyAsr = nullptr;
  if (type == DisplayItemType::TYPE_STICKY_POSITION) {
    // For sticky position items, the ASR is computed differently depending on
    // whether the item has a fixed descendant or not. But for WebRender
    // purposes we always want to use the ASR that would have been used if it
    // didn't have fixed descendants, which is stored as the "container ASR" on
    // the sticky item.
    auto* sticky = static_cast<nsDisplayStickyPosition*>(aItem);
    asr = sticky->GetContainerASR();
    stickyAsr = ActiveScrolledRoot::GetStickyASRFromFrame(sticky->Frame());
    MOZ_ASSERT(stickyAsr);
  }

  CLIP_LOG("processing item %p (%s) asr %p clip %p, inherited = %p\n", aItem,
           DisplayItemTypeName(aItem->GetType()), asr, clip,
           inheritedClipChain);

  // In most cases we can combine the leaf of the clip chain with the clip rect
  // of the display item. This reduces the number of clip items, which avoids
  // some overhead further down the pipeline.
  // Container display items are not currently supported because the clip
  // rect of a stacking context is not handled the same as normal display
  // items.
  const bool separateLeaf = clip && clip->mASR == asr &&
                            clip->mClip.GetRoundedRectCount() == 0 &&
                            !aItem->GetChildren();

  // Zoom display items report their bounds etc using the parent document's
  // APD because zoom items act as a conversion layer between the two different
  // APDs.
  const int32_t auPerDevPixel = [&] {
    if (type == DisplayItemType::TYPE_ZOOM) {
      return static_cast<nsDisplayZoom*>(aItem)->GetParentAppUnitsPerDevPixel();
    }
    return aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
  }();

  ItemClips clips(asr, clip, auPerDevPixel, separateLeaf);
  MOZ_ASSERT(!mItemClipStack.empty());
  if (clips.HasSameInputs(mItemClipStack.top())) {
    // Early-exit because if the clips are the same as aItem's previous sibling,
    // then we don't need to do do the work of popping the old stuff and then
    // pushing it right back on for the new item. Note that if aItem doesn't
    // have a previous sibling, that means BeginList would have been called
    // just before this, which will have pushed a ItemClips(nullptr, nullptr)
    // onto mItemClipStack, so the HasSameInputs check should return false.
    CLIP_LOG("\tearly-exit for %p\n", aItem);
    auto& clips = mItemClipStack.top();
    if (!clips.mClipChainId && clips.mChain) {
      clips.mClipChainId =
          DefineClipChain(clips.mChain, clips.mAppUnitsPerDevPixel);
    }
    return wr::WrSpaceAndClipChain{clips.mScrollId, clips.mClipChainId
                                                        ? clips.mClipChainId->id
                                                        : wr::ROOT_CLIP_CHAIN};
  }

  // Pop aItem's previous sibling's stuff from mBuilder in preparation for
  // pushing aItem's stuff.
  mItemClipStack.pop();

  // If the leaf of the clip chain is going to be merged with the display item's
  // clip rect, then we should create a clip chain id from the leaf's parent.
  if (separateLeaf) {
    CLIP_LOG("\tseparate leaf detected, ignoring the last clip\n");
    clip = clip->mParent;
  }

  // There are up to three ASR chains here that we need to be fully defined:
  //  1. The ASR chain pointed to by |asr|
  //  2. The ASR chain pointed to by clip->mASR
  //  3. For a sticky item, the ASR chain pointed to by the item's sticky ASR.
  //     This one is needed so that when we create WebRender commands for the
  //     sticky item, we can give WebRender accurate information about the
  //     spatial node we're in.
  // These chains will often be the same, or one will include the other,
  // but we can't rely on that always being the case, so we make a
  // DefineSpatialNodes call on all three. These calls will recursively
  // define all the ASRs that we care about for this item, but will not
  // actually push anything onto the WR stack.
  (void)DefineSpatialNodes(aBuilder, asr, aItem);
  if (clip && clip->mASR != asr) {
    (void)DefineSpatialNodes(aBuilder, clip->mASR, aItem);
  }
  if (stickyAsr && stickyAsr != asr) {
    (void)DefineSpatialNodes(aBuilder, stickyAsr, aItem);
  }

  // Define all the clips in the item's clip chain, and obtain a clip chain id
  // for it.
  clips.mClipChainId = DefineClipChain(clip, auPerDevPixel);

  wr::WrSpatialId space = GetSpatialId(asr);
  clips.mScrollId = SpatialIdAfterOverride(space);
  CLIP_LOG("\tassigning %d -> %d\n", (int)space.id, (int)clips.mScrollId.id);

  // Now that we have the scroll id and a clip id for the item, push it onto
  // the WR stack.
  clips.UpdateSeparateLeaf(*mBuilder, auPerDevPixel);
  const wr::WrSpaceAndClipChain spaceAndClipChain{
      clips.mScrollId,
      clips.mClipChainId ? clips.mClipChainId->id : wr::ROOT_CLIP_CHAIN};

  CLIP_LOG("  push: clip: %p, asr: %p, scroll = %" PRIuPTR ", clip = %" PRIu64
           "\n",
           clips.mChain, clips.mASR, spaceAndClipChain.space.id,
           spaceAndClipChain.clip_chain);

  mItemClipStack.push(clips);

  CLIP_LOG("done setup for %p\n", aItem);
  return spaceAndClipChain;
}

wr::WrSpatialId ClipManager::GetSpatialId(const ActiveScrolledRoot* aASR) {
  for (const ActiveScrolledRoot* asr = aASR; asr; asr = asr->mParent) {
    Maybe<wr::WrSpatialId> space = Nothing();
    if (asr->mKind == ActiveScrolledRoot::ASRKind::Sticky) {
      space = mBuilder->GetSpatialIdForDefinedStickyLayer(asr);
    } else {
      space = mBuilder->GetScrollIdForDefinedScrollLayer(asr->GetViewId());
    }
    if (space) {
      return *space;
    }

    // If this ASR doesn't have a scroll ID, then we should check its ancestor.
    // There may not be one defined because the ASR may not be scrollable or we
    // failed to get the scroll metadata.
  }

  Maybe<wr::WrSpatialId> space = mBuilder->GetScrollIdForDefinedScrollLayer(
      ScrollableLayerGuid::NULL_SCROLL_ID);
  MOZ_ASSERT(space.isSome());
  return *space;
}

StickyScrollContainer* ClipManager::GetStickyScrollContainer(
    const ActiveScrolledRoot* aASR) {
  MOZ_ASSERT(aASR->mKind == ActiveScrolledRoot::ASRKind::Sticky);
  StickyScrollContainer* stickyScrollContainer =
      StickyScrollContainer::GetOrCreateForFrame(aASR->mFrame);
  if (stickyScrollContainer) {
    // If there's no ASR for the scrollframe that this sticky item is attached
    // to, then don't create a WR sticky item for it either. Trying to do so
    // will end in sadness because WR will interpret some coordinates as
    // relative to the nearest enclosing scrollframe, which will correspond
    // to the nearest ancestor ASR on the gecko side. That ASR will not be the
    // same as the scrollframe this sticky item is actually supposed to be
    // attached to, thus the sadness.
    // Not sending WR the sticky item is ok, because the enclosing scrollframe
    // will never be asynchronously scrolled. Instead we will always position
    // the sticky items correctly on the gecko side and WR will never need to
    // adjust their position itself.
    if (!stickyScrollContainer->ScrollContainer()
             ->IsMaybeAsynchronouslyScrolled()) {
      stickyScrollContainer = nullptr;
    }
  }
  return stickyScrollContainer;
}

// Returns the smallest distance from "0" to the range [min, max] where
// min <= max. Despite the name, the return value is actually a 1-D vector,
// and so may be negative if max < 0.
static nscoord DistanceToRange(nscoord min, nscoord max) {
  MOZ_ASSERT(min <= max);
  if (max < 0) {
    return max;
  }
  if (min > 0) {
    return min;
  }
  MOZ_ASSERT(min <= 0 && max >= 0);
  return 0;
}

// Returns the magnitude of the part of the range [min, max] that is greater
// than zero. The return value is always non-negative.
static nscoord PositivePart(nscoord min, nscoord max) {
  MOZ_ASSERT(min <= max);
  if (min >= 0) {
    return max - min;
  }
  if (max > 0) {
    return max;
  }
  return 0;
}

// Returns the magnitude of the part of the range [min, max] that is less
// than zero. The return value is always non-negative.
static nscoord NegativePart(nscoord min, nscoord max) {
  MOZ_ASSERT(min <= max);
  if (max <= 0) {
    return max - min;
  }
  if (min < 0) {
    return 0 - min;
  }
  return 0;
}

Maybe<wr::WrSpatialId> ClipManager::DefineStickyNode(
    nsDisplayListBuilder* aBuilder, Maybe<wr::WrSpatialId> aParentSpatialId,
    const ActiveScrolledRoot* aASR, nsDisplayItem* aItem) {
  nsIFrame* stickyFrame = aASR->mFrame;

  if (Maybe<wr::WrSpatialId> space =
          mBuilder->GetSpatialIdForDefinedStickyLayer(aASR)) {
    return space;
  }

  StickyScrollContainer* stickyScrollContainer = GetStickyScrollContainer(aASR);
  if (!stickyScrollContainer) {
    // This may indicated a sticky item that does not need a webrender spatial
    // node. See the comment in GetStickyScrollContainer for details.
    return Nothing();
  }

  // Do not create a spatial node for sticky items with mShouldFlatten=true.
  // These are inside inactive scroll frames and so cannot move asynchronously.
  if (stickyScrollContainer->ShouldFlattenAway()) {
    return Nothing();
  }

  float auPerDevPixel = stickyFrame->PresContext()->AppUnitsPerDevPixel();

  nsRect itemBounds;

  // Make both itemBounds and scrollPort be relative to the reference frame
  // of the sticky frame's parent.
  nsRect scrollPort =
      stickyScrollContainer->ScrollContainer()->GetScrollPortRect();
  const nsIFrame* referenceFrame =
      aBuilder->FindReferenceFrameFor(stickyFrame->GetParent());
  nsRect transformedBounds = stickyFrame->GetRectRelativeToSelf();
  DebugOnly transformResult = nsLayoutUtils::TransformRect(
      stickyFrame, referenceFrame, transformedBounds);
  MOZ_ASSERT(transformResult == nsLayoutUtils::TRANSFORM_SUCCEEDED);
  itemBounds = transformedBounds;
  scrollPort = scrollPort +
               stickyScrollContainer->ScrollContainer()->GetOffsetToCrossDoc(
                   referenceFrame);

  Maybe<float> topMargin;
  Maybe<float> rightMargin;
  Maybe<float> bottomMargin;
  Maybe<float> leftMargin;
  wr::StickyOffsetBounds vBounds = {0.0, 0.0};
  wr::StickyOffsetBounds hBounds = {0.0, 0.0};
  nsPoint appliedOffset;

  nsRectAbsolute outer;
  nsRectAbsolute inner;
  stickyScrollContainer->GetScrollRanges(stickyFrame, &outer, &inner);

  // The following computations make more sense upon understanding the
  // semantics of "inner" and "outer", which is explained in the comment on
  // SetStickyPositionData in Layers.h.

  if (outer.YMost() != inner.YMost()) {
    // Question: How far will itemBounds.y be from the top of the scrollport
    // when we have scrolled from the current scroll position of "0" to
    // reach the range [inner.YMost(), outer.YMost()] where the item gets
    // stuck?
    // Answer: the current distance is "itemBounds.y - scrollPort.y". That
    // needs to be adjusted by the distance to the range, less any other
    // sticky ranges that fall between 0 and the range. If the distance is
    // negative (i.e. inner.YMost() <= outer.YMost() < 0) then we would be
    // scrolling upwards (decreasing scroll offset) to reach that range,
    // which would increase itemBounds.y and make it farther away from the
    // top of the scrollport. So in that case the adjustment is -distance.
    // If the distance is positive (0 < inner.YMost() <= outer.YMost()) then
    // we would be scrolling downwards, itemBounds.y would decrease, and we
    // again need to adjust by -distance. If we are already in the range
    // then no adjustment is needed and distance is 0 so again using
    // -distance works. If the distance is positive, and the item has both
    // top and bottom sticky ranges, then the bottom sticky range may fall
    // (entirely[1] or partly[2]) between the current scroll position.
    // [1]: 0 <= outer.Y() <= inner.Y() < inner.YMost() <= outer.YMost()
    // [2]: outer.Y() < 0 <= inner.Y() < inner.YMost() <= outer.YMost()
    // In these cases, the item doesn't actually move for that part of the
    // distance, so we need to subtract out that bit, which can be computed
    // as the positive portion of the range [outer.Y(), inner.Y()].
    nscoord distance = DistanceToRange(inner.YMost(), outer.YMost());
    if (distance > 0) {
      distance -= PositivePart(outer.Y(), inner.Y());
    }
    topMargin = Some(NSAppUnitsToFloatPixels(
        itemBounds.y - scrollPort.y - distance, auPerDevPixel));
    // Question: What is the maximum positive ("downward") offset that WR
    // will have to apply to this item in order to prevent the item from
    // visually moving?
    // Answer: Since the item is "sticky" in the range [inner.YMost(),
    // outer.YMost()], the maximum offset will be the size of the range, which
    // is outer.YMost() - inner.YMost().
    vBounds.max =
        NSAppUnitsToFloatPixels(outer.YMost() - inner.YMost(), auPerDevPixel);
    // Question: how much of an offset has layout already applied to the item?
    // Answer: if we are
    // (a) inside the sticky range (inner.YMost() < 0 <= outer.YMost()), or
    // (b) past the sticky range (inner.YMost() < outer.YMost() < 0)
    // then layout has already applied some offset to the position of the
    // item. The amount of the adjustment is |0 - inner.YMost()| in case (a)
    // and |outer.YMost() - inner.YMost()| in case (b).
    if (inner.YMost() < 0) {
      appliedOffset.y = std::min(0, outer.YMost()) - inner.YMost();
      MOZ_ASSERT(appliedOffset.y > 0);
    }
  }
  if (outer.Y() != inner.Y()) {
    // Similar logic as in the previous section, but this time we care about
    // the distance from itemBounds.YMost() to scrollPort.YMost().
    nscoord distance = DistanceToRange(outer.Y(), inner.Y());
    if (distance < 0) {
      distance += NegativePart(inner.YMost(), outer.YMost());
    }
    bottomMargin = Some(NSAppUnitsToFloatPixels(
        scrollPort.YMost() - itemBounds.YMost() + distance, auPerDevPixel));
    // And here WR will be moving the item upwards rather than downwards so
    // again things are inverted from the previous block.
    vBounds.min = NSAppUnitsToFloatPixels(outer.Y() - inner.Y(), auPerDevPixel);
    // We can't have appliedOffset be both positive and negative, and the top
    // adjustment takes priority. So here we only update appliedOffset.y if
    // it wasn't set by the top-sticky case above.
    if (appliedOffset.y == 0 && inner.Y() > 0) {
      appliedOffset.y = std::max(0, outer.Y()) - inner.Y();
      MOZ_ASSERT(appliedOffset.y < 0);
    }
  }
  // Same as above, but for the x-axis
  if (outer.XMost() != inner.XMost()) {
    nscoord distance = DistanceToRange(inner.XMost(), outer.XMost());
    if (distance > 0) {
      distance -= PositivePart(outer.X(), inner.X());
    }
    leftMargin = Some(NSAppUnitsToFloatPixels(
        itemBounds.x - scrollPort.x - distance, auPerDevPixel));
    hBounds.max =
        NSAppUnitsToFloatPixels(outer.XMost() - inner.XMost(), auPerDevPixel);
    if (inner.XMost() < 0) {
      appliedOffset.x = std::min(0, outer.XMost()) - inner.XMost();
      MOZ_ASSERT(appliedOffset.x > 0);
    }
  }
  if (outer.X() != inner.X()) {
    nscoord distance = DistanceToRange(outer.X(), inner.X());
    if (distance < 0) {
      distance += NegativePart(inner.XMost(), outer.XMost());
    }
    rightMargin = Some(NSAppUnitsToFloatPixels(
        scrollPort.XMost() - itemBounds.XMost() + distance, auPerDevPixel));
    hBounds.min = NSAppUnitsToFloatPixels(outer.X() - inner.X(), auPerDevPixel);
    if (appliedOffset.x == 0 && inner.X() > 0) {
      appliedOffset.x = std::max(0, outer.X()) - inner.X();
      MOZ_ASSERT(appliedOffset.x < 0);
    }
  }

  LayoutDeviceRect bounds =
      LayoutDeviceRect::FromAppUnits(itemBounds, auPerDevPixel);
  wr::LayoutVector2D applied = {
      NSAppUnitsToFloatPixels(appliedOffset.x, auPerDevPixel),
      NSAppUnitsToFloatPixels(appliedOffset.y, auPerDevPixel)};
  bool needsProp =
      nsDisplayStickyPosition::ShouldGetStickyAnimationId(stickyFrame);
  Maybe<wr::WrAnimationProperty> prop;
  auto displayItemKey = nsDisplayItem::GetPerFrameKey(
      0, 0, DisplayItemType::TYPE_STICKY_POSITION);
  auto spatialKey = wr::SpatialKey(uint64_t(stickyFrame), displayItemKey,
                                   wr::SpatialKeyKind::Sticky);
  if (needsProp) {
    RefPtr<WebRenderAPZAnimationData> animationData =
        mManager->CommandBuilder()
            .CreateOrRecycleWebRenderUserData<WebRenderAPZAnimationData>(
                displayItemKey, stickyFrame);
    uint64_t animationId = animationData->GetAnimationId();

    prop.emplace();
    prop->id = animationId;
    prop->key = spatialKey;
    prop->effect_type = wr::WrAnimationType::Transform;
  }
  wr::WrSpatialId spatialId = mBuilder->DefineStickyFrame(
      aASR, aParentSpatialId, wr::ToLayoutRect(bounds),
      topMargin.ptrOr(nullptr), rightMargin.ptrOr(nullptr),
      bottomMargin.ptrOr(nullptr), leftMargin.ptrOr(nullptr), vBounds, hBounds,
      applied, spatialKey, prop.ptrOr(nullptr));

  return Some(spatialId);
}

Maybe<wr::WrSpatialId> ClipManager::DefineSpatialNodes(
    nsDisplayListBuilder* aBuilder, const ActiveScrolledRoot* aASR,
    nsDisplayItem* aItem) {
  if (!aASR) {
    // Recursion base case
    return Nothing();
  }

  ScrollableLayerGuid::ViewID viewId = ScrollableLayerGuid::NULL_SCROLL_ID;
  if (aASR->mKind == ActiveScrolledRoot::ASRKind::Scroll) {
    viewId = aASR->GetViewId();
    Maybe<wr::WrSpatialId> space =
        mBuilder->GetScrollIdForDefinedScrollLayer(viewId);
    if (space) {
      // If we've already defined this scroll layer before, we can early-exit
      return space;
    }
  }

  // Recurse to define the ancestors
  Maybe<wr::WrSpatialId> ancestorSpace =
      DefineSpatialNodes(aBuilder, aASR->mParent, aItem);

  if (aASR->mKind == ActiveScrolledRoot::ASRKind::Sticky) {
    Maybe<wr::WrSpatialId> parent = ancestorSpace.map(
        [this](wr::WrSpatialId& aId) { return SpatialIdAfterOverride(aId); });
    return ClipManager::DefineStickyNode(aBuilder, parent, aASR, aItem);
  }

  MOZ_ASSERT(viewId != ScrollableLayerGuid::NULL_SCROLL_ID);

  ScrollContainerFrame* scrollContainerFrame = aASR->ScrollFrame();
  Maybe<ScrollMetadata> metadata = scrollContainerFrame->ComputeScrollMetadata(
      mManager, aItem->Frame(), aItem->ToReferenceFrame());
  if (!metadata) {
    MOZ_ASSERT_UNREACHABLE("Expected scroll metadata to be available!");
    return ancestorSpace;
  }

  FrameMetrics& metrics = metadata->GetMetrics();
  if (!metrics.IsScrollable()) {
    // This item is a scrolling no-op, skip over it in the ASR chain.
    return ancestorSpace;
  }

  nsPoint offset = scrollContainerFrame->GetOffsetToCrossDoc(aItem->Frame()) +
                   aItem->ToReferenceFrame();
  int32_t auPerDevPixel = aItem->Frame()->PresContext()->AppUnitsPerDevPixel();
  nsRect scrollPort = scrollContainerFrame->GetScrollPortRect() + offset;
  LayoutDeviceRect clipBounds =
      LayoutDeviceRect::FromAppUnits(scrollPort, auPerDevPixel);

  // The content rect that we hand to PushScrollLayer should be relative to
  // the same origin as the clipBounds that we hand to PushScrollLayer -
  // that is, both of them should be relative to the stacking context `aSc`.
  // However, when we get the scrollable rect from the FrameMetrics, the
  // origin has nothing to do with the position of the frame but instead
  // represents the minimum allowed scroll offset of the scrollable content.
  // While APZ uses this to clamp the scroll position, we don't need to send
  // this to WebRender at all. Instead, we take the position from the
  // composition bounds.
  LayoutDeviceRect contentRect =
      metrics.GetExpandedScrollableRect() * metrics.GetDevPixelsPerCSSPixel();
  contentRect.MoveTo(clipBounds.TopLeft());

  Maybe<wr::WrSpatialId> parent = ancestorSpace;
  if (parent) {
    *parent = SpatialIdAfterOverride(*parent);
  }
  // The external scroll offset is accumulated into the local space positions of
  // display items inside WR, so that the elements hash (intern) to the same
  // content ID for quick comparisons. To avoid invalidations when the
  // auPerDevPixel is not a round value, round here directly from app units.
  // This guarantees we won't introduce any inaccuracy in the external scroll
  // offset passed to WR.
  const bool useRoundedOffset =
      StaticPrefs::apz_rounded_external_scroll_offset();
  LayoutDevicePoint scrollOffset =
      useRoundedOffset
          ? LayoutDevicePoint::FromAppUnitsRounded(
                scrollContainerFrame->GetScrollPosition(), auPerDevPixel)
          : LayoutDevicePoint::FromAppUnits(
                scrollContainerFrame->GetScrollPosition(), auPerDevPixel);

  // Currently we track scroll-linked effects at the granularity of documents,
  // not scroll frames, so we consider a scroll frame to have a scroll-linked
  // effect whenever its containing document does.
  nsPresContext* presContext = aItem->Frame()->PresContext();
  const bool hasScrollLinkedEffect =
      !StaticPrefs::apz_disable_for_scroll_linked_effects() &&
      presContext->Document()->HasScrollLinkedEffect();

  return Some(mBuilder->DefineScrollLayer(
      viewId, parent, wr::ToLayoutRect(contentRect),
      wr::ToLayoutRect(clipBounds), wr::ToLayoutVector2D(scrollOffset),
      wr::ToWrAPZScrollGeneration(
          scrollContainerFrame->ScrollGenerationOnApz()),
      wr::ToWrHasScrollLinkedEffect(hasScrollLinkedEffect),
      wr::SpatialKey(uint64_t(scrollContainerFrame), 0,
                     wr::SpatialKeyKind::Scroll)));
}

Maybe<wr::WrClipChainId> ClipManager::DefineClipChain(
    const DisplayItemClipChain* aChain, int32_t aAppUnitsPerDevPixel) {
  MOZ_ASSERT(mCacheStackTop > 0);
  if (!aChain) {
    return Nothing();
  }

  ClipIdMap& cache = mCacheStack[mCacheStackTop - 1];
  MOZ_DIAGNOSTIC_ASSERT(aChain->mOnStack || !aChain->mASR ||
                        aChain->mASR->mFrame);

  if (auto iter = cache.find(aChain); iter != cache.end()) {
    // Found it in the currently-active cache, so just use the id we have for
    // it.
    CLIP_LOG("cache[%p] => hit\n", aChain);
    return iter->second.mWrChainID;
  }

  const auto parentChain =
      DefineClipChain(aChain->mParent, aAppUnitsPerDevPixel);
  if (!aChain->mClip.HasClip()) {
    cache[aChain] = {parentChain};
    // This item in the chain is a no-op, skip over it
    return parentChain;
  }

  auto clip = LayoutDeviceRect::FromAppUnits(aChain->mClip.GetClipRect(),
                                             aAppUnitsPerDevPixel);
  AutoTArray<wr::ComplexClipRegion, 6> wrRoundedRects;
  aChain->mClip.ToComplexClipRegions(aAppUnitsPerDevPixel, wrRoundedRects);
  wr::WrSpatialId space = GetSpatialId(aChain->mASR);
  // Define the clip
  space = SpatialIdAfterOverride(space);
  // Iterate through the clips in the current item's clip chain, define them
  // in WR, and put their IDs into |clipIds|.
  AutoTArray<wr::WrClipId, 6> clipChainClipIds;
  auto rectClipId =
      mBuilder->DefineRectClip(Some(space), wr::ToLayoutRect(clip));
  CLIP_LOG("cache[%p] <= %zu\n", aChain, rectClipId.id);
  clipChainClipIds.AppendElement(rectClipId);

  for (const auto& complexClip : wrRoundedRects) {
    auto complexClipId =
        mBuilder->DefineRoundedRectClip(Some(space), complexClip);
    CLIP_LOG("cache[%p] <= %zu\n", aChain, complexClipId.id);
    clipChainClipIds.AppendElement(complexClipId);
  }
  auto id = Some(mBuilder->DefineClipChain(clipChainClipIds, parentChain));
  cache[aChain] = {id};
  return id;
}

ClipManager::~ClipManager() {
  MOZ_ASSERT(!mBuilder);
  MOZ_ASSERT(mCacheStackTop == 0);
  MOZ_ASSERT(mItemClipStack.empty());
}

ClipManager::ItemClips::ItemClips(const ActiveScrolledRoot* aASR,
                                  const DisplayItemClipChain* aChain,
                                  int32_t aAppUnitsPerDevPixel,
                                  bool aSeparateLeaf)
    : mASR(aASR),
      mChain(aChain),
      mAppUnitsPerDevPixel(aAppUnitsPerDevPixel),
      mSeparateLeaf(aSeparateLeaf) {
  mScrollId = wr::wr_root_scroll_node_id();
}

void ClipManager::ItemClips::UpdateSeparateLeaf(
    wr::DisplayListBuilder& aBuilder, int32_t aAppUnitsPerDevPixel) {
  Maybe<wr::LayoutRect> clipLeaf;
  if (mSeparateLeaf) {
    MOZ_ASSERT(mChain);
    clipLeaf.emplace(wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(
        mChain->mClip.GetClipRect(), aAppUnitsPerDevPixel)));
  }

  aBuilder.SetClipChainLeaf(clipLeaf);
}

bool ClipManager::ItemClips::HasSameInputs(const ItemClips& aOther) {
  if (mASR != aOther.mASR || mChain != aOther.mChain ||
      mSeparateLeaf != aOther.mSeparateLeaf) {
    return false;
  }
  // AUPDP only matters if we have a clip chain, since it's only used to compute
  // the device space clip rect.
  if (mChain && mAppUnitsPerDevPixel != aOther.mAppUnitsPerDevPixel) {
    return false;
  }
  return true;
}

}  // namespace mozilla::layers
