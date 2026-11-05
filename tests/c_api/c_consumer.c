#include <ananarchiver/c_api/ananarchiver.h>

#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(aa_status) == 4, "status width");
_Static_assert(sizeof(aa_task_id) == 8, "task ID width");
_Static_assert(sizeof(aa_api_info) == 32, "ABI v1 info layout");
_Static_assert(sizeof(aa_open_request) == 40, "ABI v1 open layout");
_Static_assert(sizeof(aa_extract_request) == 96, "ABI v1 extraction layout");
_Static_assert(sizeof(aa_compress_request) == 64, "ABI v1 compression layout");
_Static_assert(sizeof(aa_entry) == 64, "ABI v1 entry layout");
_Static_assert(sizeof(aa_task_snapshot) == 216, "ABI v1 snapshot layout");
_Static_assert(offsetof(aa_string, size) == 8, "64-bit desktop string layout");

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "C consumer failed at line %d\n", __LINE__);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    aa_api_info info = {0};
    aa_engine* engine = NULL;
    const aa_error* error = NULL;
    const aa_capability_batch* capabilities = NULL;
    uint32_t stopped = 0;
    info.struct_size = sizeof(info);
    CHECK(aa_get_api_info(&info) == AA_OK);
    CHECK(info.abi_version == AA_ABI_VERSION && info.max_batch_entries == 512);
    CHECK((info.features & AA_FEATURE_ASYNC_CLEANUP) != 0);
    CHECK((info.features & AA_FEATURE_BROWSER) != 0);
    CHECK(aa_engine_create(0, &engine, &error) == AA_ABI_MISMATCH);
    CHECK(engine == NULL && error == NULL);
    CHECK(aa_engine_create(info.abi_version, &engine, &error) == AA_OK);
    CHECK(engine != NULL && error == NULL);
    CHECK(aa_compression_capabilities(engine, &capabilities, &error) == AA_OK);
    CHECK(capabilities != NULL && capabilities->count > 0);
    CHECK(capabilities->capabilities[0].struct_size == sizeof(aa_compression_capability));
    CHECK(aa_task_cancel(engine, 0) == AA_NOT_FOUND);
    CHECK(aa_engine_shutdown(engine) == AA_OK);
    CHECK(aa_engine_shutdown(engine) == AA_OK);
    CHECK(aa_engine_is_stopped(engine, &stopped) == AA_OK);
    aa_engine_destroy(engine);
    /* Provider-owned results deliberately survive engine destruction. */
    CHECK(capabilities->capabilities[0].format_name.size > 0);
    aa_capability_batch_release(capabilities);
    aa_capability_batch_release(NULL);
    aa_error_release(NULL);
    aa_entry_batch_release(NULL);
    aa_snapshot_release(NULL);
    aa_browser_result_release(NULL);
    aa_engine_destroy(NULL);
    puts("C ABI consumer passed");
    return 0;
}
