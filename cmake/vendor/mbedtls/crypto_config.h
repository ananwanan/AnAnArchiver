// TF-PSA-Crypto's shared PSA state must be protected when archive tasks run
// concurrently. Windows libarchive uses CNG; POSIX uses the pthread adapter.
#pragma once
#if !defined(_WIN32)
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
#endif
