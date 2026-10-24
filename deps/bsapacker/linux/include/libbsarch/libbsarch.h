#pragma once

// Linux replacement of the C header of libbsarch (Windows-only), spec of Phase 4e,
// section 3.1: only the types that the bsapacker sources use, with libbsarch's names and
// values. Archives are written by libbsarchpp (../../bs_archive_auto.cpp).

#include <cstdint>

typedef enum bsa_result_code_e
{
  BSA_RESULT_NONE      = 0,
  BSA_RESULT_EXCEPTION = -1
} bsa_result_code_t;

typedef void* bsa_buffer_t;
typedef void* bsa_archive_t;

typedef struct bsa_dds_info_s
{
  uint32_t width, height, mipmaps;
} bsa_dds_info_t;

struct bsa_result_message_s
{
  int8_t code;  // bsa_result_code_t
  wchar_t text[1024];
};
typedef struct bsa_result_message_s bsa_result_message_t;

struct bsa_result_buffer_s
{
  uint32_t size;
  bsa_buffer_t data;
};
typedef struct bsa_result_buffer_s bsa_result_buffer_t;

struct bsa_result_message_buffer_s
{
  bsa_result_buffer_t buffer;
  bsa_result_message_t message;
};
typedef struct bsa_result_message_buffer_s bsa_result_message_buffer_t;

typedef enum bsa_archive_type_e
{
  baNone,
  baTES3,
  baTES4,
  baFO3,
  baSSE,
  baFO4,
  baFO4dds,
  baSF,
  baSFdds
} bsa_archive_type_t;

typedef void (*bsa_file_dds_info_proc_t)(bsa_archive_t archive, const wchar_t* file_path,
                                         bsa_dds_info_t* dds_info, void* context);
