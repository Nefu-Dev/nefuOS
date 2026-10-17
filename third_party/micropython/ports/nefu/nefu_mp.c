/*
 * nefu_mp.c — MicroPython port glue for nefuOS (host build).
 *
 * Provides: interpreter output routing, GC, fatal-error hooks, file-lexer
 * stubs, and the embedding API used by the terminal command layer.
 *
 * This file is compiled as C with gcc (not g++), same as the py/ core,
 * because MicroPython is a C code base.  It relies on the host CRT for
 * malloc/printf/setjmp, which is present in the nefuOS win32 backend.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "py/stream.h"
#include "py/objstr.h"
#include "py/obj.h"
#include "py/builtin.h"
#include "py/misc.h"
#include "shared/runtime/gchelper.h"

#include "nefu_mp.h"

/* DEBUG_printf(): used by MicroPython core files when MICROPY_DEBUG_VERBOSE
   is enabled; routes to the terminal output callback. */
#if MICROPY_DEBUG_VERBOSE
int DEBUG_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mp_vprintf(&mp_plat_print, fmt, ap);
    va_end(ap);
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* output routing                                                      */
/* ------------------------------------------------------------------ */
static nefu_mp_output_fn s_out = NULL;

void nefu_mp_stdout(const char *str, size_t len) {
    if (s_out && str) {
        /* call in chunks; the callback is the terminal print sink */
        s_out(str, (int)len);
    }
}

void nefu_mp_set_output(nefu_mp_output_fn fn) {
    s_out = fn;
}

/* ------------------------------------------------------------------ */
/* nefuOS VFS bridge (installed by the C++ terminal layer)             */
/* ------------------------------------------------------------------ */
static const nefu_vfs_t *s_vfs = NULL;

void nefu_mp_set_vfs(const nefu_vfs_t *vfs) {
    s_vfs = vfs;
}

/* ------------------------------------------------------------------ */
/* fatal hooks (MicroPython aborts here on fatal internal errors)      */
/* ------------------------------------------------------------------ */
void nlr_jump_fail(void *val) {
    (void)val;
    for (;;) {
        /* hang; a bare-metal port would halt, host prints a marker */
    }
}

void NORETURN __fatal_error(const char *msg) {
    if (s_out) {
        s_out("FATAL: ", 7);
        s_out(msg, (int)strlen(msg));
        s_out("\n", 1);
    }
    for (;;) {
    }
}

/* ------------------------------------------------------------------ */
/* lex-from-file / import-stat: backed by the nefuOS VFS bridge.       */
/* ------------------------------------------------------------------ */
#if MICROPY_ENABLE_EXTERNAL_IMPORT
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    if (!s_vfs || !s_vfs->read_file) {
        mp_raise_OSError(MP_ENOENT);
    }
    uint32_t len = 0;
    char *buf = s_vfs->read_file(qstr_str(filename), &len);
    if (!buf) {
        mp_raise_OSError(MP_ENOENT);
    }
    /* the mem reader references (does not copy) the buffer and frees it with
       m_free on close, so hand it GC memory we own: copy + release the
       kalloc'd VFS copy. */
    char *owned = m_new(char, len ? len : 1);
    memcpy(owned, buf, len);
    s_vfs->free(buf);
    return mp_lexer_new_from_str_len(filename, owned, len, len);
}

mp_import_stat_t mp_import_stat(const char *path) {
    if (s_vfs && s_vfs->exists && s_vfs->exists(path)) {
        return MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}
#else
/* eval/exec/compile can lex from a file name; with external import off this
   only matters for those builtins, so raise "no such file". */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}
#endif

/* sys.stdin/stdout/stderr: the type is irrelevant (only the pointer is used),
   but it must be a valid object so base-type dispatch never crashes. */
/* sys.stdin/stdout/stderr: real stream objects.  With MICROPY_PY_IO the
   REPL/print machinery writes through mp_sys_stdout as a stream, so each
   object must carry a stream protocol; write routes to nefu_mp_stdout(). */
static mp_uint_t nefu_stdstream_read(mp_obj_t self, void *buf, mp_uint_t size, int *errcode) {
    (void)self;
    (void)buf;
    (void)size;
    *errcode = 0;
    return 0; /* EOF */
}
static mp_uint_t nefu_stdstream_write(mp_obj_t self, const void *buf, mp_uint_t size, int *errcode) {
    (void)self;
    if (s_out && buf) {
        s_out(buf, (int)size);
    }
    *errcode = 0;
    return size;
}
static mp_uint_t nefu_stdstream_ioctl(mp_obj_t self, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)self;
    (void)arg;
    switch (request) {
        case MP_STREAM_CLOSE:
        case MP_STREAM_FLUSH:
            return 0;
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}
static const mp_stream_p_t nefu_stdstream_p = {
    .read = nefu_stdstream_read,
    .write = nefu_stdstream_write,
    .ioctl = nefu_stdstream_ioctl,
};
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_nefu_stdstream,
    MP_QSTR_nefuStream,
    MP_TYPE_FLAG_NONE,
    protocol, &nefu_stdstream_p
    );

struct _mp_dummy_t {
    mp_obj_base_t base;
};
const struct _mp_dummy_t mp_sys_stdin_obj = {{(mp_obj_t)&mp_type_nefu_stdstream}};
const struct _mp_dummy_t mp_sys_stdout_obj = {{(mp_obj_t)&mp_type_nefu_stdstream}};
const struct _mp_dummy_t mp_sys_stderr_obj = {{(mp_obj_t)&mp_type_nefu_stdstream}};

/* ------------------------------------------------------------------ */
/* open() / file objects: memory-backed, served from the nefuOS VFS.   */
/*   Reading a file pulls it whole into a buffer; writes accumulate in */
/*   the buffer and are committed with a whole-file write on close/    */
/*   flush.  Seek/tell work on the in-memory cursor.                   */
/* ------------------------------------------------------------------ */
#if MICROPY_PY_IO
typedef struct _mp_obj_nefu_file_t {
    mp_obj_base_t base;
    char *path;      /* malloc'd path copy */
    char *data;      /* malloc'd buffer */
    uint32_t size;   /* current data length */
    uint32_t cap;    /* allocated capacity */
    uint32_t pos;    /* cursor */
    bool writable;   /* mode allows write */
    bool dirty;      /* buffer changed; needs write_file on close/flush */
    bool closed;
} mp_obj_nefu_file_t;

static void nefu_file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_nefu_file_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%s %s>", mp_obj_get_type_str(self_in), self->path ? self->path : "?");
}

static int nefu_file_flush(mp_obj_nefu_file_t *o) {
    if (o->closed) {
        return 0;
    }
    if (o->writable && o->dirty && s_vfs && s_vfs->write_file) {
        if (s_vfs->write_file(o->path, o->data, o->size) == 0) {
            o->dirty = false;
        }
    }
    return 0;
}

static mp_uint_t nefu_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    mp_obj_nefu_file_t *o = MP_OBJ_TO_PTR(o_in);
    if (o->closed || !buf) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    if (o->pos >= o->size) {
        return 0;
    }
    mp_uint_t n = o->size - o->pos;
    if (size < n) {
        n = size;
    }
    memcpy(buf, o->data + o->pos, n);
    o->pos += n;
    return n;
}

static mp_uint_t nefu_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_obj_nefu_file_t *o = MP_OBJ_TO_PTR(o_in);
    if (o->closed || !o->writable) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    if (o->pos + size > o->cap) {
        uint32_t ncap = o->pos + size + 256;
        char *nd = (char *)realloc(o->data, ncap);
        if (!nd) {
            *errcode = MP_ENOMEM;
            return MP_STREAM_ERROR;
        }
        o->data = nd;
        o->cap = ncap;
    }
    memcpy(o->data + o->pos, buf, size);
    o->pos += size;
    if (o->pos > o->size) {
        o->size = o->pos;
    }
    o->dirty = true;
    return size;
}

static mp_uint_t nefu_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    mp_obj_nefu_file_t *o = MP_OBJ_TO_PTR(o_in);
    switch (request) {
        case MP_STREAM_CLOSE:
            nefu_file_flush(o);
            free(o->data);
            o->data = NULL;
            free(o->path);
            o->path = NULL;
            o->closed = true;
            return 0;
        case MP_STREAM_FLUSH:
            nefu_file_flush(o);
            return 0;
        case MP_STREAM_SEEK: {
            struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
            long newpos;
            if (s->whence == 0) newpos = s->offset;
            else if (s->whence == 1) newpos = (long)o->pos + s->offset;
            else if (s->whence == 2) newpos = (long)o->size + s->offset;
            else { *errcode = MP_EINVAL; return MP_STREAM_ERROR; }
            if (newpos < 0) { *errcode = MP_EINVAL; return MP_STREAM_ERROR; }
            o->pos = (uint32_t)newpos;
            s->offset = newpos;
            return 0;
        }
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_rom_map_elem_t nefu_rawfile_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(nefu_rawfile_locals_dict, nefu_rawfile_locals_dict_table);

static const mp_stream_p_t nefu_fileio_stream_p = {
    .read = nefu_file_read,
    .write = nefu_file_write,
    .ioctl = nefu_file_ioctl,
};
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_nefu_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, nefu_file_print,
    protocol, &nefu_fileio_stream_p,
    locals_dict, &nefu_rawfile_locals_dict
    );

static const mp_stream_p_t nefu_textio_stream_p = {
    .read = nefu_file_read,
    .write = nefu_file_write,
    .ioctl = nefu_file_ioctl,
    .is_text = true,
};
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_nefu_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, nefu_file_print,
    protocol, &nefu_textio_stream_p,
    locals_dict, &nefu_rawfile_locals_dict
    );

/* the open() builtin (port-provided per py/builtin.h) */
static mp_obj_t nefu_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    if (!s_vfs || !s_vfs->read_file || !s_vfs->write_file) {
        mp_raise_OSError(MP_ENOENT);
    }
    const char *path = mp_obj_str_get_str(args[0]);
    const char *mode = "r";
    if (n_args > 1) {
        mode = mp_obj_str_get_str(args[1]);
    }
    bool read = false, write = false, append = false, text = true;
    for (const char *p = mode; *p; p++) {
        switch (*p) {
            case 'r': read = true; break;
            case 'w': write = true; break;
            case 'a': append = true; break;
            case 'b': text = false; break;
            case 't': text = true; break;
            case '+': read = write = true; break;
        }
    }
    if (!read && !write && !append) {
        read = true;
    }
    const mp_obj_type_t *type = text ? &mp_type_nefu_textio : &mp_type_nefu_fileio;

    mp_obj_nefu_file_t *o = mp_obj_malloc_with_finaliser(mp_obj_nefu_file_t, type);
    o->path = malloc(strlen(path) + 1);
    if (!o->path) {
        mp_raise_OSError(MP_ENOMEM);
    }
    strcpy(o->path, path);
    o->data = NULL;
    o->size = 0;
    o->cap = 0;
    o->pos = 0;
    o->writable = write || append;
    o->dirty = false;
    o->closed = false;

    /* load existing content unless opening with 'w' (truncate) */
    bool want_existing = read || append;
    if (want_existing) {
        uint32_t elen = 0;
        char *ebuf = s_vfs->read_file(path, &elen);
        if (ebuf) {
            o->data = malloc(elen + 1);
            if (!o->data) {
                free(o->path);
                o->path = NULL;
                mp_raise_OSError(MP_ENOMEM);
            }
            memcpy(o->data, ebuf, elen);
            o->data[elen] = 0;
            o->size = elen;
            o->cap = elen + 1;
            s_vfs->free(ebuf);
        } else if (!write && !append) {
            /* 'r' of a missing file */
            free(o->path);
            o->path = NULL;
            mp_raise_OSError(MP_ENOENT);
        }
        /* 'a' of a missing file: start empty */
    }
    if (append) {
        o->pos = o->size;
    }
    return MP_OBJ_FROM_PTR(o);
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    return nefu_builtin_open(n_args, args, kwargs);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, nefu_builtin_open);

#endif /* MICROPY_PY_IO */

/* ------------------------------------------------------------------ */
/* GC collection: walk callee-saved registers + stack via setjmp.      */
/* ------------------------------------------------------------------ */
#if MICROPY_ENABLE_GC
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
#endif

/* ------------------------------------------------------------------ */
/* static heap                                                         */
/* ------------------------------------------------------------------ */
#if MICROPY_ENABLE_GC
static char heap[MICROPY_HEAP_SIZE];
#endif

/* ------------------------------------------------------------------ */
/* embedding API                                                       */
/* ------------------------------------------------------------------ */
static int s_initialised = 0;

static int do_exec(const char *src, mp_parse_input_kind_t input_kind);

void nefu_mp_init(void) {
    if (s_initialised) {
        return;
    }
#if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
#endif
    mp_init();

    /* configure the C-stack guard: mp_init() resets stack_top, and the port
       is responsible for installing a limit, otherwise mp_stack_check()
       (enabled at EXTRA_FEATURES) raises "maximum recursion depth" on every
       call because the limit defaults to 0. */
    {
        char stack_dummy;
        mp_stack_set_top(&stack_dummy);
        mp_stack_set_limit(128 * 1024);
    }

    /* Append the pip install directory to sys.path so packages installed by
       `pip` (written into /usr/share/python on the nefuOS VFS) become
       importable.  The directory is created lazily by pip on first use. */
    do_exec("import sys\nsys.path.append('/usr/share/python')\n", MP_PARSE_FILE_INPUT);

    s_initialised = 1;
}

void nefu_mp_deinit(void) {
    if (!s_initialised) {
        return;
    }
    mp_deinit();
    s_initialised = 0;
}

static int do_exec(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        /* uncaught exception: print it to the terminal output callback */
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return 1;
    }
}

int nefu_mp_exec(const char *src, int file_input) {
    if (!src) {
        return 1;
    }
    if (!s_initialised) {
        nefu_mp_init();
    }
    return do_exec(src, file_input ? MP_PARSE_FILE_INPUT : MP_PARSE_SINGLE_INPUT);
}

const char *nefu_mp_version(void) {
    return MICROPY_VERSION_STRING;
}
