/*
 * nefu_mp.h — C embedding API of MicroPython for nefuOS.
 *
 * The nefuOS terminal command layer (C++) calls these functions to run
 * Python source.  Output produced by the interpreter is routed to the
 * callback installed with nefu_mp_set_output() (chunked, no newline
 * interpretation).
 */
#ifndef NEFU_MP_H
#define NEFU_MP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output callback: receives a chunk of interpreter output (len bytes). */
typedef void (*nefu_mp_output_fn)(const char *s, int len);

/* Initialise / finalise the interpreter (heap + runtime). */
void nefu_mp_init(void);
void nefu_mp_deinit(void);

/* Install the sink for interpreter output (may be NULL to discard). */
void nefu_mp_set_output(nefu_mp_output_fn fn);

/*
 * Execute Python source.
 *   src        : the source text (must be NUL terminated)
 *   file_input : 1 => MP_PARSE_FILE_INPUT (multi-statement script),
 *                0 => MP_PARSE_SINGLE_INPUT (single interactive statement)
 * Returns 0 on success, non-zero if an exception was raised (printed).
 */
int nefu_mp_exec(const char *src, int file_input);

/* Return a short version string such as "MicroPython v1.24.1". */
const char *nefu_mp_version(void);

/* ---- nefuOS VFS bridge -----------------------------------------------
   The interpreter's file I/O (open()), external `import`, and file stat
   are backed by the nefuOS filesystem through these C callbacks, which the
   C++ terminal layer installs with nefu_mp_set_vfs().  All callbacks must
   be thread-safe for the duration of an exec. */

typedef struct _nefu_vfs {
    /* read a whole file into a fresh buffer.  Returns a NUL-terminated
       buffer (*len set to the byte count) that must be released with free(),
       or NULL if the path does not resolve to a file. */
    char *(*read_file)(const char *path, uint32_t *len);
    /* write a whole file (create if missing, truncate otherwise).  Returns
       0 on success, non-zero on failure. */
    int (*write_file)(const char *path, const char *data, uint32_t len);
    /* return 1 if path resolves to an existing file or dir, else 0. */
    int (*exists)(const char *path);
    /* release a buffer returned by read_file. */
    void (*free)(void *p);
} nefu_vfs_t;

void nefu_mp_set_vfs(const nefu_vfs_t *vfs);

#ifdef __cplusplus
}
#endif

#endif /* NEFU_MP_H */
