/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a cut down version of base/feature_list.h.
// This just returns the default state for a feature.

#ifndef BASE_FEATURE_LIST_H_
#define BASE_FEATURE_LIST_H_

namespace base {


// Specifies whether a given feature is enabled or disabled by default.
// NOTE: The actual runtime state may be different, due to a field trial or a
// command line switch.
enum FeatureState {
  FEATURE_DISABLED_BY_DEFAULT,
  FEATURE_ENABLED_BY_DEFAULT,
};

// Recommended macros for declaring and defining features and parameters:
//
// - `kFeature` is the C++ identifier that will be used for the `base::Feature`.
// - `name` is the feature name, which must be globally unique. This name is
//   used to enable/disable features via experiments and command-line flags.
//   Names should use CamelCase-style naming, e.g. "MyGreatFeature".
// - `default_state` is the default state to use for the feature, i.e.
//   `base::FEATURE_DISABLED_BY_DEFAULT` or `base::FEATURE_ENABLED_BY_DEFAULT`.
//   As noted above, the actual runtime state may differ from the default state,
//   due to field trials or command-line switches.

// Provides a forward declaration for `kFeature` in a header file, e.g.
//
//   BASE_DECLARE_FEATURE(kMyFeature);
//
// If the feature needs to be marked as exported, i.e. it is referenced by
// multiple components, then write:
//
//   COMPONENT_EXPORT(MY_COMPONENT) BASE_DECLARE_FEATURE(kMyFeature);
#define BASE_DECLARE_FEATURE(kFeature) \
  extern CONSTINIT const base::Feature kFeature

// Provides a definition for `kFeature` with `name` and `default_state`, e.g.
//
//   BASE_FEATURE(kMyFeature, "MyFeature", base::FEATURE_DISABLED_BY_DEFAULT);
//
// Features should *not* be defined in header files; do not use this macro in
// header files.
#define BASE_FEATURE(feature, name, default_state) \
  CONSTINIT const base::Feature feature(           \
      name, default_state, base::internal::FeatureMacroHandshake::kSecret)

// Provides a forward declaration for `feature_object_name` in a header file,
// e.g.
//
//   BASE_DECLARE_FEATURE_PARAM(kMyFeatureParam);
//
// If the feature needs to be marked as exported, i.e. it is referenced by
// multiple components, then write:
//
//   COMPONENT_EXPORT(MY_COMPONENT) BASE_DECLARE_FEATURE_PARAM(kMyFeatureParam);
//
// This macro enables optimizations to make the second and later calls faster,
// but requires additional memory uses. If you obtain the parameter only once,
// you can instantiate base::FeatureParam directly, or can call
// base::GetFieldTrialParamByFeatureAsInt or equivalent functions for other
// types directly.
#define BASE_DECLARE_FEATURE_PARAM(T, feature_object_name) \
  extern CONSTINIT const base::FeatureParam<T> feature_object_name

// Provides a definition for `feature_object_name` with `T`, `feature`, `name`
// and `default_value`, with an internal parsed value cache, e.g.
//
//   BASE_FEATURE_PARAM(int, kMyFeatureParam, kMyFeature, "MyFeatureParam", 0);
//
// `T` is a parameter type, one of bool, int, size_t, double, std::string, and
// base::TimeDelta. Enum types are not supported for now.
//
// For now, ScopedFeatureList doesn't work to change the value dynamically when
// the cache is used with this macro.
//
// It should *not* be defined in header files; do not use this macro in header
// files.
#define BASE_FEATURE_PARAM(T, feature_object_name, feature, name,       \
                           default_value)                               \
  namespace field_trial_params_internal {                               \
  T GetFeatureParamWithCacheFor##feature_object_name(                   \
      const base::FeatureParam<T>* feature_param) {                     \
    static const typename base::internal::FeatureParamTraits<           \
        T>::CacheStorageType storage =                                  \
        base::internal::FeatureParamTraits<T>::ToCacheStorageType(      \
            feature_param->GetWithoutCache());                          \
    return base::internal::FeatureParamTraits<T>::FromCacheStorageType( \
        storage);                                                       \
  }                                                                     \
  } /* field_trial_params_internal */                                   \
  CONSTINIT const base::FeatureParam<T> feature_object_name(            \
      feature, name, default_value,                                     \
      &field_trial_params_internal::                                    \
          GetFeatureParamWithCacheFor##feature_object_name)

// Same as BASE_FEATURE_PARAM() but used for enum type parameters with on extra
// argument, `options`. See base::FeatureParam<Enum> template declaration in
// //base/metrics/field_trial_params.h for `options`' details.
#define BASE_FEATURE_ENUM_PARAM(T, feature_object_name, feature, name, \
                                default_value, options)                \
  namespace field_trial_params_internal {                              \
  T GetFeatureParamWithCacheFor##feature_object_name(                  \
      const base::FeatureParam<T>* feature_param) {                    \
    static const T param = feature_param->GetWithoutCache();           \
    return param;                                                      \
  }                                                                    \
  } /* field_trial_params_internal */                                  \
  CONSTINIT const base::FeatureParam<T> feature_object_name(           \
      feature, name, default_value, options,                           \
      &field_trial_params_internal::                                   \
          GetFeatureParamWithCacheFor##feature_object_name)

// Secret handshake to (try to) ensure all places that construct a base::Feature
// go through the helper `BASE_FEATURE()` macro above.
namespace internal {
enum class FeatureMacroHandshake { kSecret };
}

// The Feature struct is used to define the default state for a feature. See
// comment below for more details. There must only ever be one struct instance
// for a given feature name - generally defined as a constant global variable or
// file static. It should never be used as a constexpr as it breaks
// pointer-based identity lookup.
struct BASE_EXPORT Feature {
  constexpr Feature(const char* name, FeatureState default_state,
                    internal::FeatureMacroHandshake)
      : name(name), default_state(default_state) {
  }

  // Non-copyable since:
  // - there should be only one `Feature` instance per unique name.
  // - a `Feature` contains internal cached state about the override state.
  Feature(const Feature&) = delete;
  Feature& operator=(const Feature&) = delete;

  // The name of the feature. This should be unique to each feature and is used
  // for enabling/disabling features via command line flags and experiments.
  // It is strongly recommended to use CamelCase style for feature names, e.g.
  // "MyGreatFeature".
  const char* const name;

  // The default state (i.e. enabled or disabled) for this feature.
  const FeatureState default_state;
};

class BASE_EXPORT FeatureList {
 public:
  static bool IsEnabled(const Feature& feature) {
    return feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
  }

  static FeatureList* GetInstance() { return nullptr; }

  FeatureList(const FeatureList&) = delete;
  FeatureList& operator=(const FeatureList&) = delete;
};

}  // namespace base

#endif  // BASE_FEATURE_LIST_H_
