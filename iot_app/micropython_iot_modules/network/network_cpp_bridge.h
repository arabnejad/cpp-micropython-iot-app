#pragma once

#include "iot_native_result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Download details copied from C++ into values that the C module can read. */
typedef struct {
  const char *file_path;
  const char *sha256;
  uint64_t    size_in_bytes;
  const char *content_type;
  int         loaded_from_cache;
} iot_downloaded_file_t;

/* Downloads one HTTP or HTTPS URL into the runtime's temporary cache. */
iot_native_result_t iot_network_download_file(const char *url, const char *expected_sha256,
                                              iot_downloaded_file_t *downloaded_file);

#ifdef __cplusplus
}
#endif
