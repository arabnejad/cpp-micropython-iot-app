#pragma once

#include <xf86drmMode.h>

#include <memory>

namespace iot {
namespace display {
namespace internal {

/**
 * Calls the correct libdrm cleanup function when a DRM pointer is released.
 *
 * `DrmObjectType` is the type returned by libdrm. `ReleaseFunction` is the C
 * function that releases that type. For example, a connector uses:
 *
 *   DrmObjectType  = drmModeConnector
 *   ReleaseFunction = drmModeFreeConnector
 *
 * C++ `delete` cannot release libdrm objects. `std::unique_ptr` calls this
 * deleter automatically when its DRM object goes out of scope.
 */
template <typename DrmObjectType, void (*ReleaseFunction)(DrmObjectType *)> struct DrmObjectDeleter {
  void operator()(DrmObjectType *object) const noexcept {
    if (object != nullptr) {
      ReleaseFunction(object);
    }
  }
};

/**
 * Creates an owning C++ pointer for one libdrm object type.
 *
 * For example:
 *
 *   using DrmConnectorPtr =
 *       UniqueDrmObject<drmModeConnector, drmModeFreeConnector>;
 *
 * A `DrmConnectorPtr` can hold the pointer returned by
 * `drmModeGetConnector()`. When it leaves scope, it automatically calls
 * `drmModeFreeConnector()`.
 */
template <typename DrmObjectType, void (*ReleaseFunction)(DrmObjectType *)>
using UniqueDrmObject =
    std::unique_ptr<DrmObjectType, DrmObjectDeleter<DrmObjectType, ReleaseFunction>>;

using DrmResourcesPtr        = UniqueDrmObject<drmModeRes, drmModeFreeResources>;
using DrmConnectorPtr        = UniqueDrmObject<drmModeConnector, drmModeFreeConnector>;
using DrmEncoderPtr          = UniqueDrmObject<drmModeEncoder, drmModeFreeEncoder>;
using DrmCrtcPtr             = UniqueDrmObject<drmModeCrtc, drmModeFreeCrtc>;
using DrmObjectPropertiesPtr = UniqueDrmObject<drmModeObjectProperties, drmModeFreeObjectProperties>;
using DrmPropertyPtr         = UniqueDrmObject<drmModePropertyRes, drmModeFreeProperty>;
using DrmPropertyBlobPtr     = UniqueDrmObject<drmModePropertyBlobRes, drmModeFreePropertyBlob>;

} // namespace internal
} // namespace display
} // namespace iot
