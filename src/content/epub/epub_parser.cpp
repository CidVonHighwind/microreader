/*
 * epub_parser.c - Minimal EPUB parser implementation
 *
 * Uses tinfl (DEFLATE decompressor) directly with a custom minimal ZIP reader.
 * No heavy mz_zip_archive infrastructure - just what we need.
 */

#include "epub_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/EInkDisplay.h"
#include "../../lib/miniz.h"

extern EInkDisplay einkDisplay;

#ifdef ARDUINO
#define USE_ARDUINO_FILE 1
extern "C" {
/* External Arduino file wrappers implemented in epub_parser_arduino.cpp */
extern void* arduino_file_open(const char* path);
extern void arduino_file_close(void* handle);
extern int arduino_file_seek(void* handle, long offset, int whence);
extern long arduino_file_tell(void* handle);
extern size_t arduino_file_read(void* ptr, size_t size, size_t count, void* handle);
extern int arduino_get_free_heap(void);
extern void arduino_log_memory(const char* msg);
}
#endif

/* ZIP local file header signature */
#define ZIP_LOCAL_HEADER_SIG 0x04034b50
#define ZIP_CENTRAL_HEADER_SIG 0x02014b50
#define ZIP_END_CENTRAL_SIG 0x06054b50

/* File operation wrappers for Arduino compatibility */
#ifdef USE_ARDUINO_FILE

#define FILE_HANDLE void*
#define file_open_impl(path) arduino_file_open(path)
#define file_close_impl(handle) arduino_file_close(handle)
#define file_seek_impl(handle, offset, whence) arduino_file_seek(handle, offset, whence)
#define file_tell_impl(handle) arduino_file_tell(handle)
#define file_read_impl(ptr, size, count, handle) arduino_file_read(ptr, size, count, handle)

#else

#define FILE_HANDLE FILE*
#define file_open_impl(path) fopen(path, "rb")
#define file_close_impl(handle) fclose(handle)
#define file_seek_impl(handle, offset, whence) fseek(handle, offset, whence)
#define file_tell_impl(handle) ftell(handle)
#define file_read_impl(ptr, size, count, handle) fread(ptr, size, count, handle)

#endif

/* Central directory file entry (on-disk format) */
#pragma pack(push, 1)
typedef struct {
  uint32_t signature;
  uint16_t version_made;
  uint16_t version_needed;
  uint16_t flags;
  uint16_t compression;
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_len;
  uint16_t comment_len;
  uint16_t disk_start;
  uint16_t internal_attr;
  uint32_t external_attr;
  uint32_t local_header_offset;
} zip_central_dir_entry;

typedef struct {
  uint32_t signature;
  uint16_t disk_num;
  uint16_t central_dir_disk;
  uint16_t entries_this_disk;
  uint16_t total_entries;
  uint32_t central_dir_size;
  uint32_t central_dir_offset;
  uint16_t comment_len;
} zip_end_central_dir;
#pragma pack(pop)

/* Minimal file entry in memory */
typedef struct {
  char* filename;
  uint64_t compressed_size;
  uint64_t uncompressed_size;
  uint32_t local_header_offset;
  uint16_t compression;
} file_entry;

/* EPUB reader structure */
struct epub_reader {
#ifdef USE_ARDUINO_FILE
  void* file_handle; /* Arduino File object pointer */
#else
  FILE* fp;
#endif
  file_entry* files;
  uint32_t file_count;
  epub_error last_error;
};

/* Pull-based streaming context */
struct epub_stream_context {
  epub_reader* reader;
  file_entry* entry;

  /* Decompression state */
  tinfl_decompressor* inflator;
  uint8_t* in_buf; /* Input buffer for compressed data */
  uint8_t* dict;   /* Decompression dictionary (32KB) */

  size_t chunk_size;
  size_t in_remaining;  /* Compressed bytes left to read */
  size_t in_buf_size;   /* Valid bytes in input buffer */
  size_t in_buf_ofs;    /* Current offset in input buffer */
  size_t dict_ofs;      /* Write offset in dictionary (where next decompressed data goes) */
  size_t dict_read_ofs; /* Read offset in dictionary (where next output should come from) */
  size_t dict_avail;    /* Bytes available in dictionary for reading */

  tinfl_status status;
  int done;  /* 1 if decompression complete */
  int error; /* 1 if error occurred */
};

/* Find end of central directory record */
static int find_end_central_dir(FILE_HANDLE fp, zip_end_central_dir* eocd) {
  uint8_t buf[1024];
  long file_size;

  file_seek_impl(fp, 0, SEEK_END);
  file_size = file_tell_impl(fp);

  /* Search last 1KB for end signature */
  long search_start = (file_size > 1024) ? (file_size - 1024) : 0;
  file_seek_impl(fp, search_start, SEEK_SET);
  size_t read_size = file_read_impl(buf, 1, 1024, fp);

  /* Search backwards for signature */
  for (int i = read_size - 22; i >= 0; i--) {
    uint32_t* sig = (uint32_t*)&buf[i];
    if (*sig == ZIP_END_CENTRAL_SIG) {
      memcpy(eocd, &buf[i], sizeof(zip_end_central_dir));
      return 1;
    }
  }

  return 0;
}

/* Read central directory and build file list */
static epub_error read_central_directory(epub_reader* reader, zip_end_central_dir* eocd) {
  reader->file_count = eocd->total_entries;
  reader->files = (file_entry*)calloc(reader->file_count, sizeof(file_entry));
  if (!reader->files) {
    return EPUB_ERROR_OUT_OF_MEMORY;
  }
#ifdef USE_ARDUINO_FILE
  {
    char msg[128];
    snprintf(msg, sizeof(msg), "  [MEM] read_central_directory: allocated files[%u], Free=%d",
             (unsigned)reader->file_count, arduino_get_free_heap());
    arduino_log_memory(msg);
  }
#else
  printf("  [MEM] read_central_directory: allocated files[%u]\n", (unsigned)reader->file_count);
#endif

  /* Seek to central directory */
#ifdef USE_ARDUINO_FILE
  file_seek_impl(reader->file_handle, eocd->central_dir_offset, SEEK_SET);
#else
  file_seek_impl(reader->fp, eocd->central_dir_offset, SEEK_SET);
#endif

  /* Read each entry */
  const unsigned LOG_INTERVAL = 50;
  size_t last_logged_free = 0;
  for (uint32_t i = 0; i < reader->file_count; i++) {
    zip_central_dir_entry entry;
#ifdef USE_ARDUINO_FILE
    if (file_read_impl(&entry, sizeof(zip_central_dir_entry), 1, reader->file_handle) != 1)
#else
    if (file_read_impl(&entry, sizeof(zip_central_dir_entry), 1, reader->fp) != 1)
#endif
    {
      return EPUB_ERROR_CORRUPTED;
    }

    if (entry.signature != ZIP_CENTRAL_HEADER_SIG) {
      return EPUB_ERROR_CORRUPTED;
    }

    /* Read filename */
    char* filename = (char*)malloc(entry.filename_len + 1);
    if (!filename) {
      return EPUB_ERROR_OUT_OF_MEMORY;
    }
#ifdef USE_ARDUINO_FILE
    {
      int freeNow = arduino_get_free_heap();
      if ((i % LOG_INTERVAL) == 0 || last_logged_free == 0 ||
          (last_logged_free > freeNow && (last_logged_free - freeNow) > 4096)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "  [MEM] read_central_directory: allocated filename buffer %u bytes for entry %u, Free=%d\n",
                 (unsigned)(entry.filename_len + 1), (unsigned)i, freeNow);
        arduino_log_memory(msg);
        last_logged_free = freeNow;
      }
    }
#else
    if ((i % LOG_INTERVAL) == 0) {
      printf("  [MEM] read_central_directory: allocated filename buffer %u bytes for entry %u\n",
             (unsigned)(entry.filename_len + 1), (unsigned)i);
    }
#endif

#ifdef USE_ARDUINO_FILE
    if (file_read_impl(filename, 1, entry.filename_len, reader->file_handle) != entry.filename_len)
#else
    if (file_read_impl(filename, 1, entry.filename_len, reader->fp) != entry.filename_len)
#endif
    {
      free(filename);
      return EPUB_ERROR_CORRUPTED;
    }
    filename[entry.filename_len] = '\0';

    /* Skip extra field and comment */
#ifdef USE_ARDUINO_FILE
    file_seek_impl(reader->file_handle, entry.extra_len + entry.comment_len, SEEK_CUR);
#else
    file_seek_impl(reader->fp, entry.extra_len + entry.comment_len, SEEK_CUR);
#endif

    /* Store file info */
    reader->files[i].filename = filename;
    reader->files[i].compressed_size = entry.compressed_size;
    reader->files[i].uncompressed_size = entry.uncompressed_size;
    reader->files[i].local_header_offset = entry.local_header_offset;
    reader->files[i].compression = entry.compression;
  }

  return EPUB_OK;
}

/* -------------------- Public API -------------------- */

epub_error epub_open(const char* filepath, epub_reader** out_reader) {
  if (!filepath || !out_reader) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  epub_reader* reader = (epub_reader*)calloc(1, sizeof(epub_reader));
  if (!reader) {
    return EPUB_ERROR_OUT_OF_MEMORY;
  }

#ifdef USE_ARDUINO_FILE
  reader->file_handle = file_open_impl(filepath);
  if (!reader->file_handle) {
    free(reader);
    return EPUB_ERROR_FILE_NOT_FOUND;
  }

  /* Find and read end of central directory */
  zip_end_central_dir eocd;
  if (!find_end_central_dir(reader->file_handle, &eocd)) {
    file_close_impl(reader->file_handle);
    free(reader);
    return EPUB_ERROR_NOT_AN_EPUB;
  }

  /* Read central directory */
  epub_error err = read_central_directory(reader, &eocd);
  if (err != EPUB_OK) {
    file_close_impl(reader->file_handle);
    free(reader);
    return err;
  }
#else
  reader->fp = file_open_impl(filepath);
  if (!reader->fp) {
    free(reader);
    return EPUB_ERROR_FILE_NOT_FOUND;
  }

  /* Find and read end of central directory */
  zip_end_central_dir eocd;
  if (!find_end_central_dir(reader->fp, &eocd)) {
    file_close_impl(reader->fp);
    free(reader);
    return EPUB_ERROR_NOT_AN_EPUB;
  }

  /* Read central directory */
  epub_error err = read_central_directory(reader, &eocd);
  if (err != EPUB_OK) {
    file_close_impl(reader->fp);
    free(reader);
    return err;
  }
#endif

  *out_reader = reader;
  return EPUB_OK;
}

void epub_close(epub_reader* reader) {
  if (reader) {
    if (reader->files) {
      for (uint32_t i = 0; i < reader->file_count; i++) {
        free(reader->files[i].filename);
      }
      free(reader->files);
    }
#ifdef USE_ARDUINO_FILE
    if (reader->file_handle) {
      file_close_impl(reader->file_handle);
    }
#else
    if (reader->fp) {
      file_close_impl(reader->fp);
    }
#endif
    free(reader);
  }
}

uint32_t epub_get_file_count(epub_reader* reader) {
  return reader ? reader->file_count : 0;
}

epub_error epub_get_file_info(epub_reader* reader, uint32_t index, epub_file_info* info) {
  if (!reader || !info || index >= reader->file_count) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  file_entry* entry = &reader->files[index];
  strncpy(info->filename, entry->filename, sizeof(info->filename) - 1);
  info->filename[sizeof(info->filename) - 1] = '\0';
  info->compressed_size = entry->compressed_size;
  info->uncompressed_size = entry->uncompressed_size;
  info->file_offset = entry->local_header_offset;
  info->compression = entry->compression;

  return EPUB_OK;
}

epub_error epub_locate_file(epub_reader* reader, const char* filename, uint32_t* out_index) {
  if (!reader || !filename || !out_index) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  for (uint32_t i = 0; i < reader->file_count; i++) {
    if (strcmp(reader->files[i].filename, filename) == 0) {
      *out_index = i;
      return EPUB_OK;
    }
  }

  return EPUB_ERROR_FILE_NOT_IN_ARCHIVE;
}

epub_error epub_extract_streaming(epub_reader* reader, uint32_t file_index, epub_data_callback callback,
                                  void* user_data) {
  if (!reader || !callback || file_index >= reader->file_count) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  file_entry* entry = &reader->files[file_index];

#ifdef USE_ARDUINO_FILE
  FILE_HANDLE fp = reader->file_handle;
#else
  FILE_HANDLE fp = reader->fp;
#endif

  /* Seek to local file header */
  file_seek_impl(fp, entry->local_header_offset, SEEK_SET);

  /* Read local header to skip to data */
  uint32_t sig;
  uint16_t version_needed, flags, compression_method;
  file_read_impl(&sig, 4, 1, fp);
  if (sig != ZIP_LOCAL_HEADER_SIG) {
    return EPUB_ERROR_CORRUPTED;
  }

  file_read_impl(&version_needed, 2, 1, fp);
  file_read_impl(&flags, 2, 1, fp);
  file_read_impl(&compression_method, 2, 1, fp);

  file_seek_impl(fp, 16, SEEK_CUR); /* Skip rest of header to filename length */
  uint16_t filename_len, extra_len;
  file_read_impl(&filename_len, 2, 1, fp);
  file_read_impl(&extra_len, 2, 1, fp);

  file_seek_impl(fp, filename_len + extra_len, SEEK_CUR);

  /* Now at compressed data */

  if (entry->compression == 0) {
    /* Stored (uncompressed) */
    uint8_t* buffer = einkDisplay.getFrameBuffer();
    size_t chunk_size = EInkDisplay::BUFFER_SIZE;

    size_t remaining = entry->uncompressed_size;
    while (remaining > 0) {
      size_t to_read = (remaining < chunk_size) ? remaining : chunk_size;
      size_t read_size = file_read_impl(buffer, 1, to_read, fp);
      if (read_size == 0) {
        // free(buffer);
        return EPUB_ERROR_EXTRACTION_FAILED;
      }

      if (!callback(buffer, read_size, user_data)) {
        // free(buffer);
        return EPUB_OK; /* User cancelled */
      }

      remaining -= read_size;
    }

    // free(buffer);
    return EPUB_OK;
  } else if (entry->compression == 8) {
    /* DEFLATE compression - use tinfl with dictionary */
    size_t fixed_size = sizeof(tinfl_decompressor) + TINFL_LZ_DICT_SIZE;
    uint8_t* memory_block = einkDisplay.getFrameBuffer();
    size_t chunk_size = EInkDisplay::BUFFER_SIZE - fixed_size;

    /* Partition the block */
    tinfl_decompressor* inflator = (tinfl_decompressor*)memory_block;
    uint8_t* dict = memory_block + sizeof(tinfl_decompressor);
    uint8_t* in_buf = dict + TINFL_LZ_DICT_SIZE;

    memset(inflator, 0, sizeof(tinfl_decompressor));
    memset(dict, 0, TINFL_LZ_DICT_SIZE); /* Initialize dictionary to zero */

    tinfl_init(inflator);

    size_t in_remaining = entry->compressed_size;
    size_t in_buf_size = 0;
    size_t in_buf_ofs = 0;
    size_t dict_ofs = 0;
    tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

    while (status == TINFL_STATUS_NEEDS_MORE_INPUT || status == TINFL_STATUS_HAS_MORE_OUTPUT) {
      /* Read more compressed data if needed */
      if (in_buf_ofs >= in_buf_size && in_remaining > 0) {
        size_t to_read = (in_remaining < chunk_size) ? in_remaining : chunk_size;
        in_buf_size = file_read_impl(in_buf, 1, to_read, fp);
        if (in_buf_size == 0) {
          return EPUB_ERROR_EXTRACTION_FAILED;
        }
        in_remaining -= in_buf_size;
        in_buf_ofs = 0;
      }

      size_t in_bytes = in_buf_size - in_buf_ofs;
      size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;

      /* ZIP files use raw DEFLATE without ZLIB wrapper */
      /* Use wrapping output buffer (dictionary mode) since we're using a 32KB sliding window */
      mz_uint32 flags = 0;
      /* Only set HAS_MORE_INPUT if we truly have more compressed data to read */
      if (in_remaining > 0) {
        flags |= TINFL_FLAG_HAS_MORE_INPUT;
      }

      status = tinfl_decompress_raw(inflator, in_buf + in_buf_ofs, &in_bytes, dict, dict + dict_ofs, &out_bytes, flags);

      in_buf_ofs += in_bytes;

      if (out_bytes > 0) {
        int cb_result = callback(dict + dict_ofs, out_bytes, user_data);
        if (cb_result == 0) {
          return EPUB_ERROR_EXTRACTION_FAILED;
        }
        dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
      }

      if (status < TINFL_STATUS_DONE) {
        return EPUB_ERROR_EXTRACTION_FAILED;
      }
    }

    return EPUB_OK;
  }
  return EPUB_ERROR_EXTRACTION_FAILED;
}

/* -------------------- Pull-based Streaming API -------------------- */

epub_stream_context* epub_start_streaming(epub_reader* reader, uint32_t file_index) {
  if (!reader || file_index >= reader->file_count) {
    return NULL;
  }

  file_entry* entry = &reader->files[file_index];

  /* Allocate context */
  epub_stream_context* ctx = (epub_stream_context*)calloc(1, sizeof(epub_stream_context));
  if (!ctx) {
    return NULL;
  }
#ifdef USE_ARDUINO_FILE
  {
    char msg[128];
    snprintf(msg, sizeof(msg), "  [MEM] epub_start_streaming: allocated ctx (%u bytes), Free=%d\n",
             (unsigned)sizeof(epub_stream_context), arduino_get_free_heap());
    arduino_log_memory(msg);
  }
#else
  printf("  [MEM] epub_start_streaming: allocated ctx (%u bytes)\n", (unsigned)sizeof(epub_stream_context));
#endif

  ctx->reader = reader;
  ctx->entry = entry;
  ctx->done = 0;
  ctx->error = 0;

  /* Only DEFLATE compression supported for streaming (stored files are simple enough to handle inline) */
  if (entry->compression != 8 && entry->compression != 0) {
    free(ctx);
    return NULL;
  }

#ifdef USE_ARDUINO_FILE
  FILE_HANDLE fp = reader->file_handle;
#else
  FILE_HANDLE fp = reader->fp;
#endif

  /* Seek to local file header */
  file_seek_impl(fp, entry->local_header_offset, SEEK_SET);

  /* Read local header to skip to data */
  uint32_t sig;
  uint16_t version_needed, flags, compression_method;
  file_read_impl(&sig, 4, 1, fp);
  if (sig != ZIP_LOCAL_HEADER_SIG) {
    free(ctx);
    return NULL;
  }

  file_read_impl(&version_needed, 2, 1, fp);
  file_read_impl(&flags, 2, 1, fp);
  file_read_impl(&compression_method, 2, 1, fp);

  file_seek_impl(fp, 16, SEEK_CUR);
  uint16_t filename_len, extra_len;
  file_read_impl(&filename_len, 2, 1, fp);
  file_read_impl(&extra_len, 2, 1, fp);

  file_seek_impl(fp, filename_len + extra_len, SEEK_CUR);

  /* Now at compressed data */

  if (entry->compression == 8) {
    /* DEFLATE - allocate decompression buffers */
    size_t fixed_size = sizeof(tinfl_decompressor) + TINFL_LZ_DICT_SIZE;
    uint8_t* memory_block = einkDisplay.getFrameBuffer();
    ctx->chunk_size = EInkDisplay::BUFFER_SIZE - fixed_size;

    /* Partition the block */
    ctx->inflator = (tinfl_decompressor*)memory_block;
    ctx->dict = memory_block + sizeof(tinfl_decompressor);
    ctx->in_buf = ctx->dict + TINFL_LZ_DICT_SIZE;

    memset(ctx->inflator, 0, sizeof(tinfl_decompressor));
    memset(ctx->dict, 0, TINFL_LZ_DICT_SIZE);
    tinfl_init(ctx->inflator);

    ctx->in_remaining = entry->compressed_size;
    ctx->in_buf_size = 0;
    ctx->in_buf_ofs = 0;
    ctx->dict_ofs = 0;
    ctx->dict_read_ofs = 0;
    ctx->dict_avail = 0;
    ctx->status = TINFL_STATUS_NEEDS_MORE_INPUT;
  } else {
    /* Stored (uncompressed) - simpler, just need input buffer */
    ctx->in_buf = einkDisplay.getFrameBuffer();
    ctx->chunk_size = EInkDisplay::BUFFER_SIZE;
    ctx->in_remaining = entry->uncompressed_size;
  }

  return ctx;
}

int epub_read_chunk(epub_stream_context* ctx, void* buffer, size_t max_size) {
  if (!ctx || ctx->error) {
    return -1;
  }

  if (ctx->done) {
    return 0; /* EOF */
  }

#ifdef USE_ARDUINO_FILE
  FILE_HANDLE fp = ctx->reader->file_handle;
#else
  FILE_HANDLE fp = ctx->reader->fp;
#endif

  if (ctx->entry->compression == 0) {
    /* Stored (uncompressed) - just read directly */
    if (ctx->in_remaining == 0) {
      ctx->done = 1;
      return 0;
    }

    size_t to_read = (ctx->in_remaining < max_size) ? ctx->in_remaining : max_size;
    size_t read_size = file_read_impl(buffer, 1, to_read, fp);
    if (read_size == 0) {
      ctx->error = 1;
      return -1;
    }

    ctx->in_remaining -= read_size;
    if (ctx->in_remaining == 0) {
      ctx->done = 1;
    }

    return (int)read_size;

  } else if (ctx->entry->compression == 8) {
    /* DEFLATE - decompress next chunk */
    size_t output_ofs = 0;

    /* First, output any remaining data from previous decompression that didn't fit */
    while (output_ofs < max_size && ctx->dict_avail > 0) {
      size_t to_copy = ctx->dict_avail;
      if (output_ofs + to_copy > max_size) {
        to_copy = max_size - output_ofs;
      }

      /* Handle wraparound in circular dictionary buffer */
      size_t first_chunk = TINFL_LZ_DICT_SIZE - ctx->dict_read_ofs;
      if (first_chunk > to_copy) {
        first_chunk = to_copy;
      }

      memcpy((uint8_t*)buffer + output_ofs, ctx->dict + ctx->dict_read_ofs, first_chunk);
      output_ofs += first_chunk;
      ctx->dict_read_ofs = (ctx->dict_read_ofs + first_chunk) & (TINFL_LZ_DICT_SIZE - 1);
      ctx->dict_avail -= first_chunk;

      /* If there's more and we wrapped around */
      if (first_chunk < to_copy && ctx->dict_avail > 0) {
        size_t second_chunk = to_copy - first_chunk;
        memcpy((uint8_t*)buffer + output_ofs, ctx->dict + ctx->dict_read_ofs, second_chunk);
        output_ofs += second_chunk;
        ctx->dict_read_ofs = (ctx->dict_read_ofs + second_chunk) & (TINFL_LZ_DICT_SIZE - 1);
        ctx->dict_avail -= second_chunk;
      }
    }

    /* If buffer is full or we're done, return what we have */
    if (output_ofs >= max_size || ctx->done) {
      return (int)output_ofs;
    }

    /* Decompress more data */
    while (output_ofs < max_size &&
           (ctx->status == TINFL_STATUS_NEEDS_MORE_INPUT || ctx->status == TINFL_STATUS_HAS_MORE_OUTPUT)) {
      /* Read more compressed data if needed */
      if (ctx->in_buf_ofs >= ctx->in_buf_size && ctx->in_remaining > 0) {
        size_t to_read = (ctx->in_remaining < ctx->chunk_size) ? ctx->in_remaining : ctx->chunk_size;
        ctx->in_buf_size = file_read_impl(ctx->in_buf, 1, to_read, fp);
        if (ctx->in_buf_size == 0) {
          ctx->error = 1;
          return -1;
        }
        ctx->in_remaining -= ctx->in_buf_size;
        ctx->in_buf_ofs = 0;
      }

      size_t in_bytes = ctx->in_buf_size - ctx->in_buf_ofs;
      size_t out_bytes = TINFL_LZ_DICT_SIZE - ctx->dict_ofs;

      mz_uint32 flags = 0;
      if (ctx->in_remaining > 0) {
        flags |= TINFL_FLAG_HAS_MORE_INPUT;
      }

      ctx->status = tinfl_decompress_raw(ctx->inflator, ctx->in_buf + ctx->in_buf_ofs, &in_bytes, ctx->dict,
                                         ctx->dict + ctx->dict_ofs, &out_bytes, flags);

      ctx->in_buf_ofs += in_bytes;

      if (out_bytes > 0) {
        /* Copy decompressed data to output buffer */
        size_t to_copy = out_bytes;
        if (output_ofs + to_copy > max_size) {
          to_copy = max_size - output_ofs;
        }

        memcpy((uint8_t*)buffer + output_ofs, ctx->dict + ctx->dict_ofs, to_copy);
        output_ofs += to_copy;

        /* Advance write position in dictionary */
        ctx->dict_ofs = (ctx->dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);

        /* If we couldn't copy all, save the remainder for next call */
        if (to_copy < out_bytes) {
          ctx->dict_read_ofs = (ctx->dict_ofs - (out_bytes - to_copy)) & (TINFL_LZ_DICT_SIZE - 1);
          ctx->dict_avail = out_bytes - to_copy;
          break;
        }
      }

      if (ctx->status < TINFL_STATUS_DONE) {
        ctx->error = 1;
        return -1;
      }

      if (ctx->status == TINFL_STATUS_DONE) {
        ctx->done = 1;
        break;
      }
    }

    return (int)output_ofs;
  }

  ctx->error = 1;
  return -1;
}

void epub_end_streaming(epub_stream_context* ctx) {
  if (ctx) {
    free(ctx);
  }
}

const char* epub_get_error_string(epub_error error) {
  switch (error) {
    case EPUB_OK:
      return "Success";
    case EPUB_ERROR_FILE_NOT_FOUND:
      return "File not found";
    case EPUB_ERROR_NOT_AN_EPUB:
      return "Not a valid EPUB/ZIP file";
    case EPUB_ERROR_CORRUPTED:
      return "File is corrupted";
    case EPUB_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case EPUB_ERROR_INVALID_PARAM:
      return "Invalid parameter";
    case EPUB_ERROR_EXTRACTION_FAILED:
      return "Extraction failed";
    case EPUB_ERROR_FILE_NOT_IN_ARCHIVE:
      return "File not found in archive";
    default:
      return "Unknown error";
  }
}
