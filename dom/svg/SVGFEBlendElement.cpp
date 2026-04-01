/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGFEBlendElement.h"
#include "mozilla/dom/SVGFEBlendElementBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/BindContext.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(FEBlend)

using namespace mozilla::gfx;

namespace mozilla::dom {

JSObject* SVGFEBlendElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return SVGFEBlendElement_Binding::Wrap(aCx, this, aGivenProto);
}

SVGEnumMapping SVGFEBlendElement::sModeMap[] = {
    {nsGkAtoms::normal, uint8_t(SVGFEBlendMode::Normal)},
    {nsGkAtoms::multiply, uint8_t(SVGFEBlendMode::Multiply)},
    {nsGkAtoms::screen, uint8_t(SVGFEBlendMode::Screen)},
    {nsGkAtoms::darken, uint8_t(SVGFEBlendMode::Darken)},
    {nsGkAtoms::lighten, uint8_t(SVGFEBlendMode::Lighten)},
    {nsGkAtoms::overlay, uint8_t(SVGFEBlendMode::Overlay)},
    {nsGkAtoms::colorDodge, uint8_t(SVGFEBlendMode::ColorDodge)},
    {nsGkAtoms::colorBurn, uint8_t(SVGFEBlendMode::ColorBurn)},
    {nsGkAtoms::hardLight, uint8_t(SVGFEBlendMode::HardLight)},
    {nsGkAtoms::softLight, uint8_t(SVGFEBlendMode::SoftLight)},
    {nsGkAtoms::difference, uint8_t(SVGFEBlendMode::Difference)},
    {nsGkAtoms::exclusion, uint8_t(SVGFEBlendMode::Exclusion)},
    {nsGkAtoms::hue, uint8_t(SVGFEBlendMode::Hue)},
    {nsGkAtoms::saturation, uint8_t(SVGFEBlendMode::Saturation)},
    {nsGkAtoms::color, uint8_t(SVGFEBlendMode::Color)},
    {nsGkAtoms::luminosity, uint8_t(SVGFEBlendMode::Luminosity)},
    {nullptr, 0}};

SVGElement::EnumInfo SVGFEBlendElement::sEnumInfo[1] = {
    {nsGkAtoms::mode, sModeMap, uint8_t(SVGFEBlendMode::Normal)}};

SVGElement::StringInfo SVGFEBlendElement::sStringInfo[3] = {
    {nsGkAtoms::result, kNameSpaceID_None, true},
    {nsGkAtoms::in, kNameSpaceID_None, true},
    {nsGkAtoms::in2, kNameSpaceID_None, true}};

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGFEBlendElement)

//----------------------------------------------------------------------
// nsIDOMSVGFEBlendElement methods

already_AddRefed<DOMSVGAnimatedString> SVGFEBlendElement::In1() {
  return mStringAttributes[IN1].ToDOMAnimatedString(this);
}

already_AddRefed<DOMSVGAnimatedString> SVGFEBlendElement::In2() {
  return mStringAttributes[IN2].ToDOMAnimatedString(this);
}

already_AddRefed<DOMSVGAnimatedEnumeration> SVGFEBlendElement::Mode() {
  return mEnumAttributes[MODE].ToDOMAnimatedEnum(this);
}

FilterPrimitiveDescription SVGFEBlendElement::GetPrimitiveDescription(
    SVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
    const nsTArray<bool>& aInputsAreTainted,
    nsTArray<RefPtr<SourceSurface>>& aInputImages) {
  BlendAttributes attributes;
  attributes.mBlendMode = SVGFEBlendMode(mEnumAttributes[MODE].GetAnimValue());
  return FilterPrimitiveDescription(AsVariant(std::move(attributes)));
}

bool SVGFEBlendElement::AttributeAffectsRendering(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute) const {
  return SVGFEBlendElementBase::AttributeAffectsRendering(aNameSpaceID,
                                                          aAttribute) ||
         (aNameSpaceID == kNameSpaceID_None &&
          (aAttribute == nsGkAtoms::in || aAttribute == nsGkAtoms::in2 ||
           aAttribute == nsGkAtoms::mode));
}

void SVGFEBlendElement::GetSourceImageNames(nsTArray<SVGStringInfo>& aSources) {
  aSources.AppendElement(SVGStringInfo(&mStringAttributes[IN1], this));
  aSources.AppendElement(SVGStringInfo(&mStringAttributes[IN2], this));
}

nsresult SVGFEBlendElement::BindToTree(BindContext& aCtx, nsINode& aParent) {
  if (aCtx.InComposedDoc()) {
    aCtx.OwnerDoc().SetUseCounter(eUseCounter_custom_feBlend);
  }

  return SVGFEBlendElementBase::BindToTree(aCtx, aParent);
}

//----------------------------------------------------------------------
// SVGElement methods

SVGElement::EnumAttributesInfo SVGFEBlendElement::GetEnumInfo() {
  return EnumAttributesInfo(mEnumAttributes, sEnumInfo, std::size(sEnumInfo));
}

SVGElement::StringAttributesInfo SVGFEBlendElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

}  // namespace mozilla::dom
