/*
 * epub_parser.h - Minimal EPUB parser using tinfl directly
 *
 * This version bypasses miniz's heavy ZIP archive management and uses
 * only the DEFLATE decompressor (tinfl) with a custom minimal ZIP reader.
 * Target: <64KB total memory usage for streaming extraction.
 */

#ifndef EPUB_PARSER_H
#define EPUB_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Error codes */
    typedef enum
    {
        EPUB_OK = 0,
        EPUB_ERROR_FILE_NOT_FOUND = -1,
        EPUB_ERROR_NOT_AN_EPUB = -2,
        EPUB_ERROR_CORRUPTED = -3,
        EPUB_ERROR_OUT_OF_MEMORY = -4,
        EPUB_ERROR_INVALID_PARAM = -5,
        EPUB_ERROR_EXTRACTION_FAILED = -6,
        EPUB_ERROR_FILE_NOT_IN_ARCHIVE = -7
    } epub_error;

    /* EPUB reader context - opaque structure */
    typedef struct epub_reader epub_reader;

    /* Callback function for streaming file content */
    typedef int (*epub_data_callback)(const void *data, size_t size, void *user_data);

    /* File information structure */
    typedef struct
    {
        char filename[256];
        uint64_t compressed_size;
        uint64_t uncompressed_size;
        uint32_t file_offset; /* Offset in ZIP file */
        uint32_t compression; /* 0=stored, 8=deflate */
    } epub_file_info;

    /* -------------------- Core API -------------------- */

    /* Open an EPUB file for minimal reading */
    epub_error epub_open(const char *filepath, epub_reader **out_reader);

    /* Close and free reader */
    void epub_close(epub_reader *reader);

    /* Get total number of files */
    uint32_t epub_get_file_count(epub_reader *reader);

    /* Get file info by index */
    epub_error epub_get_file_info(epub_reader *reader, uint32_t index, epub_file_info *info);

    /* Find file by name */
    epub_error epub_locate_file(epub_reader *reader, const char *filename, uint32_t *out_index);

    /* Extract file with streaming (minimal memory) */
    epub_error epub_extract_streaming(epub_reader *reader, uint32_t file_index,
                                      epub_data_callback callback, void *user_data,
                                      size_t chunk_size);

    /* Get error string */
    const char *epub_get_error_string(epub_error error);

#ifdef __cplusplus
}
#endif

#endif /* EPUB_PARSER_H */
