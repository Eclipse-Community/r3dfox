/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_mscom_AgileReference_h
#define mozilla_mscom_AgileReference_h

#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "nsDebug.h"
#include "nsISupportsImpl.h"

#include <objidl.h>
#include <utility>

namespace mozilla::mscom {

namespace detail {
class MOZ_HEAP_CLASS GlobalInterfaceTableCookie final {
 public:
  GlobalInterfaceTableCookie(IUnknown* aObject, REFIID aIid,
                             HRESULT& aOutHResult);

  bool IsValid() const { return !!mCookie; }
  HRESULT GetInterface(REFIID aIid, void** aOutInterface) const;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GlobalInterfaceTableCookie)

  GlobalInterfaceTableCookie(const GlobalInterfaceTableCookie&) = delete;
  GlobalInterfaceTableCookie(GlobalInterfaceTableCookie&&) = delete;

  GlobalInterfaceTableCookie& operator=(const GlobalInterfaceTableCookie&) =
      delete;
  GlobalInterfaceTableCookie& operator=(GlobalInterfaceTableCookie&&) = delete;

 private:
  ~GlobalInterfaceTableCookie();

 private:
  DWORD mCookie;

 private:
  static IGlobalInterfaceTable* ObtainGit();
};

// Detemplatized implementation details of `AgileReference`.
HRESULT AgileReference_CreateImpl(
    RefPtr<IAgileReference>&, RefPtr<GlobalInterfaceTableCookie>&, REFIID,
    IUnknown*);
HRESULT AgileReference_ResolveImpl(
    RefPtr<IAgileReference> const&,
    RefPtr<GlobalInterfaceTableCookie> const&, REFIID, REFIID, void**);
}  // namespace detail

/**
 * This class encapsulates an "agile reference". These are references that allow
 * you to pass COM interfaces between apartments. When you have an interface
 * that you would like to pass between apartments, you wrap that interface in an
 * AgileReference and pass that instead. Then you can "unwrap" the interface by
 * calling Resolve(), which will return a proxy object implementing the same
 * interface.
 *
 * Sample usage:
 *
 * ```
 * // From a non-main thread, where `foo` is an `IFoo*` or `RefPtr<IFoo>`:
 * auto myAgileRef = AgileReference(foo);
 * NS_DispatchToMainThread([mar = std::move(myAgileRef)] {
 *   RefPtr<IFoo> foo = mar.Resolve();
 *   // Now methods may be invoked on `foo`
 * });
 * ```
 */
template <typename InterfaceT>
class AgileReference final {
  static_assert(
      std::is_base_of_v<IUnknown, InterfaceT>,
      "template parameter of AgileReference must be a COM interface type");

 public:
  AgileReference() : mIid(__uuidof(InterfaceT)), mHResult(E_NOINTERFACE) {}

  ~AgileReference() { Clear(); }

  AgileReference(const AgileReference& aOther) = default;
  AgileReference(AgileReference&& aOther) noexcept
      : mIid(aOther.mIid),
        mAgileRef(std::move(aOther.mAgileRef)),
        mGitCookie(std::move(aOther.mGitCookie)),
        mHResult(aOther.mHResult) {
    aOther.mHResult = CO_E_RELEASED;
  }

  AgileReference& operator=(const AgileReference& aOther) {
    if (this == &aOther) {
      return *this;
    }

    Clear();
    mIid = aOther.mIid;
    mAgileRef = aOther.mAgileRef;
    mGitCookie = aOther.mGitCookie;
    mHResult = aOther.mHResult;
    return *this;
  }

  AgileReference& operator=(AgileReference&& aOther) noexcept {
    if (this == &aOther) {
      return *this;
    }

    Clear();
    mIid = aOther.mIid;
    mAgileRef = std::move(aOther.mAgileRef);
    mGitCookie = std::move(aOther.mGitCookie);
    mHResult = aOther.mHResult;
    aOther.mHResult = CO_E_RELEASED;
    return *this;
  }

  AgileReference& operator=(std::nullptr_t) {
    Clear();
    return *this;
  }

  // Create a new AgileReference from an existing COM object.
  //
  // These constructors do not provide the HRESULT on failure. If that's
  // desired, use `AgileReference::Create()`, below.
  explicit AgileReference(InterfaceT* aObject)
      : mIid(__uuidof(InterfaceT)), mHResult(E_UNEXPECTED) {
    mHResult = detail::AgileReference_CreateImpl(
        mAgileRef, mGitCookie, mIid, aObject);
    (void)NS_WARN_IF(FAILED(mHResult));
  }
  explicit AgileReference(RefPtr<InterfaceT> const& aObject)
      : AgileReference(aObject.get()) {}

  // Create a new AgileReference from an existing COM object, or alternatively,
  // return the HRESULT explaining why one couldn't be created.
  //
  // A convenience wrapper `MakeAgileReference()` which infers `InterfaceT` from
  // the RefPtr's concrete type is provided below.
  static Result<AgileReference<InterfaceT>, HRESULT> Create(
      RefPtr<InterfaceT> const& aObject) {
    AgileReference ret;
    ret.mHResult = detail::AgileReference_CreateImpl(
        ret.mAgileRef, ret.mGitCookie, ret.mIid, aObject.get());
    if (FAILED(ret.mHResult)) {
      return Err(ret.mHResult);
    }
    return ret;
  }

  explicit operator bool() const {
    return !!mAgileRef || (mGitCookie && mGitCookie->IsValid());
  }

  HRESULT GetHResult() const { return mHResult; }

  // Common case: resolve directly to the originally-specified interface-type.
  RefPtr<InterfaceT> Resolve() const {
    auto res = ResolveAs<InterfaceT>();
    if (res.isErr()) return nullptr;
    return res.unwrap();
  }

  // Uncommon cases: resolve directly to a different interface type, and/or
  // provide IAgileReference::Resolve()'s HRESULT.
  //
  // When used in other COM apartments, `IAgileInterface::Resolve()` returns a
  // proxy object which (at time of writing) is not documented to provide any
  // interface other than the one for which it was instantiated. (Calling
  // `QueryInterface` _might_ work, but isn't explicitly guaranteed.)
  //
  template <typename OtherInterface = InterfaceT>
  Result<RefPtr<OtherInterface>, HRESULT> ResolveAs() const {
    RefPtr<OtherInterface> p;
    auto const hr = ResolveRaw(__uuidof(OtherInterface), getter_AddRefs(p));
    if (FAILED(hr)) {
      return Err(hr);
    }
    return p;
  }

  // Raw version of Resolve/ResolveAs. Rarely, if ever, preferable to the
  // statically-typed versions.
  HRESULT ResolveRaw(REFIID aIid, void** aOutInterface) const {
    return detail::AgileReference_ResolveImpl(mAgileRef, mGitCookie, mIid,
                                              aIid, aOutInterface);
  }

 private:
  void Clear() {
    mAgileRef = nullptr;
    mGitCookie = nullptr;
    mHResult = E_NOINTERFACE;
  }

 private:
  // The interface ID with which this reference was constructed.
  IID mIid;
  RefPtr<IAgileReference> mAgileRef;
  RefPtr<detail::GlobalInterfaceTableCookie> mGitCookie;
  // The result associated with this reference's construction. May be modified
  // when either backing reference changes, but is not touched by Resolve().
  HRESULT mHResult;
};

// Attempt to create an AgileReference from a refcounted interface pointer,
// providing the HRESULT as a secondary return-value.
template <typename InterfaceT>
inline Result<AgileReference<InterfaceT>, HRESULT> MakeAgileReference(
    RefPtr<InterfaceT> const& aObj) {
  return AgileReference<InterfaceT>::Create(aObj);
}

}  // namespace mozilla::mscom

#endif  // mozilla_mscom_AgileReference_h
