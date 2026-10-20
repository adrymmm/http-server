#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "http_parse.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char* buf = malloc(size);
    if (buf == NULL) return 0;
    memcpy(buf, data, size);
    struct http_request out = {0};
    parse_request(buf, size, &out);
    free(buf);
    return 0;
}
