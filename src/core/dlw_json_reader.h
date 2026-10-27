#ifndef DLUMA_JSON_READER_H
#define DLUMA_JSON_READER_H

#include "../../sdk/c/dluma_api.h"

typedef struct DlumaJsonReader {
    const unsigned char* begin;
    const unsigned char* cursor;
    const unsigned char* end;
    unsigned int depth;
    unsigned int error_offset;
    DlumaStatus status;
} DlumaJsonReader;

void dluma_json_reader_initialize(
    DlumaJsonReader* reader,
    const void* bytes,
    unsigned int size);

void dluma_json_space(DlumaJsonReader* reader);
int dluma_json_take(DlumaJsonReader* reader, unsigned char expected);
int dluma_json_object_begin(DlumaJsonReader* reader);
int dluma_json_object_next(DlumaJsonReader* reader, int* first);
int dluma_json_object_end(DlumaJsonReader* reader);
int dluma_json_array_begin(DlumaJsonReader* reader);
int dluma_json_array_next(DlumaJsonReader* reader, int* first);
int dluma_json_array_end(DlumaJsonReader* reader);
int dluma_json_string(
    DlumaJsonReader* reader,
    char* output,
    unsigned int capacity);
int dluma_json_boolean(DlumaJsonReader* reader, int* output);
int dluma_json_i32(DlumaJsonReader* reader, int* output);
int dluma_json_u64(DlumaJsonReader* reader, uint64_t* output);
int dluma_json_skip_value(DlumaJsonReader* reader);
int dluma_json_finished(DlumaJsonReader* reader);
unsigned int dluma_json_error_offset(const DlumaJsonReader* reader);
DlumaStatus dluma_json_status(const DlumaJsonReader* reader);

#endif
