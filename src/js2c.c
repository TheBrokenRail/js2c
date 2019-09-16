/*
 * QuickJS command line compiler
 * 
 * Copyright (c) 2018-2019 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include "cutils.h"

#include "js_std.h"

#include <string.h>  // strlen(), memcmp()

int strend(const char *s, const char *t) {
    size_t ls = strlen(s); // find length of s
    size_t lt = strlen(t); // find length of t
    if (ls >= lt)  // check if t can fit in s
    {
        // point s to where t should start and compare the strings from there
        return (0 == memcmp(t, s + (ls - lt), lt));
    }
    return 0; // t was longer than s
}

typedef struct {
    char *name;
    char *short_name;
    int flags;
} namelist_entry_t;

typedef struct namelist_t {
    namelist_entry_t *array;
    int count;
    int size;
} namelist_t;

static namelist_t cname_list;
static namelist_t cmodule_list;
static namelist_t init_module_list;
static FILE *outfile;
static BOOL byte_swap;

void namelist_add(namelist_t *lp, const char *name, const char *short_name,
                  int flags) {
    namelist_entry_t *e;
    if (lp->count == lp->size) {
        size_t newsize = lp->size + (lp->size >> 1) + 4;
        namelist_entry_t *a =
            realloc(lp->array, sizeof(lp->array[0]) * newsize);
        /* XXX: check for realloc failure */
        lp->array = a;
        lp->size = newsize;
    }
    e =  &lp->array[lp->count++];
    e->name = strdup(name);
    if (short_name)
        e->short_name = strdup(short_name);
    else
        e->short_name = NULL;
    e->flags = flags;
}

void namelist_free(namelist_t *lp) {
    while (lp->count > 0) {
        namelist_entry_t *e = &lp->array[--lp->count];
        free(e->name);
        free(e->short_name);
    }
    free(lp->array);
    lp->array = NULL;
    lp->size = 0;
}

namelist_entry_t *namelist_find(namelist_t *lp, const char *name) {
    int i;
    for (i = 0; i < lp->count; i++) {
        namelist_entry_t *e = &lp->array[i];
        if (!strcmp(e->name, name))
            return e;
    }
    return NULL;
}

static int file_num = 0;

static void get_c_name(char **buf) {
    asprintf(buf, "__js2c_internal_%i", file_num);
    file_num++;
}

static void dump_hex(FILE *f, const uint8_t *buf, size_t len) {
    size_t i, col;
    col = 0;
    for (i = 0; i < len; i++) {
        fprintf(f, " 0x%02x,", buf[i]);
        if (++col == 8) {
            fprintf(f, "\n");
            col = 0;
        }
    }
    if (col != 0)
        fprintf(f, "\n");
}

static void output_object_code(JSContext *ctx,
                               FILE *fo, JSValueConst obj, const char *c_name,
                               BOOL load_only) {
    uint8_t *out_buf;
    size_t out_buf_len;
    int flags;
    flags = JS_WRITE_OBJ_BYTECODE;
    if (byte_swap)
        flags |= JS_WRITE_OBJ_BSWAP;
    out_buf = JS_WriteObject(ctx, &out_buf_len, obj, flags);
    if (!out_buf) {
        js_std_dump_error(ctx);
        exit(1);
    }

    namelist_add(&cname_list, c_name, NULL, load_only);
    
    fprintf(fo, "const uint32_t %s_size = %u;\n\n", 
            c_name, (unsigned int)out_buf_len);
    fprintf(fo, "const uint8_t %s[%u] = {\n",
            c_name, (unsigned int)out_buf_len);
    dump_hex(fo, out_buf, out_buf_len);
    fprintf(fo, "};\n\n");

    js_free(ctx, out_buf);
}

static int js_module_dummy_init(JSContext *ctx, JSModuleDef *m) {
    /* should never be called when compiling JS code */
    abort();
}

JSModuleDef *jsc_module_loader(JSContext *ctx,
                              const char *module_name, void *opaque) {
    JSModuleDef *m;
    namelist_entry_t *e;

    /* check if it is a declared C or system module */
    e = namelist_find(&cmodule_list, module_name);
    if (e) {
        /* add in the static init module list */
        namelist_add(&init_module_list, e->name, e->short_name, 0);
        /* create a dummy module */
        m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
    } else if (has_suffix(module_name, ".so")) {
        fprintf(stderr, "Warning: binary module '%s' is not compiled\n", module_name);
        /* create a dummy module */
        m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
    } else {
        size_t buf_len;
        uint8_t *buf;
        JSValue func_val;
        char *cname;
        
        buf = js_load_file(ctx, &buf_len, module_name);
        if (!buf) {
            JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                                   module_name);
            return NULL;
        }
        
        /* compile the module */
        func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                           JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        js_free(ctx, buf);
        if (JS_IsException(func_val))
            return NULL;
        get_c_name(&cname);
        output_object_code(ctx, outfile, func_val, cname, TRUE);
        
        /* the module is already referenced, so we must free it */
        m = JS_VALUE_GET_PTR(func_val);
        JS_FreeValue(ctx, func_val);
    }
    return m;
}

static void compile_file(JSContext *ctx, FILE *fo,
                         const char *filename,
                         int module) {
    uint8_t *buf;
    char *c_name;
    int eval_flags;
    JSValue obj;
    size_t buf_len;
    
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        fprintf(stderr, "Could not load '%s'\n", filename);
        exit(1);
    }
    eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
    if (module < 0) {
        module = (has_suffix(filename, ".mjs") ||
                  JS_DetectModule((const char *)buf, buf_len));
    }
    if (module)
        eval_flags |= JS_EVAL_TYPE_MODULE;
    else
        eval_flags |= JS_EVAL_TYPE_GLOBAL;
    obj = JS_Eval(ctx, (const char *)buf, buf_len, filename, eval_flags);
    if (JS_IsException(obj)) {
        js_std_dump_error(ctx);
        exit(1);
    }
    js_free(ctx, buf);
    get_c_name(&c_name);
    output_object_code(ctx, fo, obj, c_name, FALSE);
    JS_FreeValue(ctx, obj);
}

static const char init_c_header[] =
    "#include \"quickjs.h\"\n"
    "#include <inttypes.h>\n"
    "\n"
    "extern void js_std_init(JSContext *);\n"
    "extern void js_std_eval_binary(JSContext *, const uint8_t *, size_t, int);\n"
    "extern void js_std_dump_error(JSContext *);\n"
    "\n"
    "static JSContext *ctx;\n"
    "static JSRuntime *rt;\n"
    "\n"
    ;

static const char init_c_template1[] =
    "void init_"
    ;

static const char init_c_template2[] =
    "()\n"
    "{\n"
    "  rt = JS_NewRuntime();\n"
    "  ctx = JS_NewContext(rt);\n"
    ;

static const char init_c_template3[] =
    "  js_std_init(ctx);\n"
    "}\n"
    "void cleanup_"
    ;

static const char init_c_template4[] =
    "()\n"
    "{\n"
    "  JS_FreeContext(ctx);\n"
    "  JS_FreeRuntime(rt);\n"
    "}\n"
    ;

void help(void) {
    printf("QuickJS version " CONFIG_VERSION "\n"
           "usage: js2c [options] [files]\n"
           "\n"
           "options are:\n"
           "-e          output in a C file (required header files are located in "CONFIG_INCLUDE_DIR")\n"
           "-c          output in an object file\n"
           "            when generating a C file or an object file, manual linking with libjs2c is required\n"
           "-o output   set the output filename\n"
           "-N cname    set the name to be used in init_<>(), and cleanup_<>() methods (default = \"js_library\")\n"
           "-m          compile as Javascript module (default=autodetect)\n"
           "-M module_name[,cname] add initialization code for an external C module\n"
           "-x          byte swapped output\n"
           );
    exit(1);
}

#if defined(CONFIG_CC) && !defined(_WIN32)

int exec_cmd(char **argv) {
    int pid, status, ret;

    pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        exit(1);
    } 

    for (;;) {
        ret = waitpid(pid, &status, 0);
        if (ret == pid && WIFEXITED(status))
            break;
    }
    return WEXITSTATUS(status);
}

static int output_executable(const char *out_filename, const char *cfilename,
                             BOOL use_lto, BOOL verbose, const char *exename, int object) {
    const char *argv[64];
    const char **arg;
    char exe_dir[1024], *p, *buf, *inc_dir, *lib_dir, *libjsname;
    int ret;
    
    /* get the directory of the executable */
    pstrcpy(exe_dir, sizeof(exe_dir), exename);
    p = strrchr(exe_dir, '/');
    if (p) {
        *p = '\0';
    } else {
        pstrcpy(exe_dir, sizeof(exe_dir), ".");
    }

    /* if 'quickjs.h' is present at the same path as the executable, we
       use it as include and lib directory */
    asprintf(&buf, "%s/quickjs.h", exe_dir);
    if (access(buf, R_OK) == 0) {
        inc_dir = exe_dir;
        lib_dir = exe_dir;
    } else {
        inc_dir = CONFIG_INCLUDE_DIR;
        lib_dir = CONFIG_LIB_DIR;
    }
    
    arg = argv;
    *arg++ = CONFIG_CC;
    if (object) {
        *arg++ = "-c";
    } else {
        *arg++ = "-shared";
        *arg++ = "-fPIC";
    }
    *arg++ = "-O2";
    /* XXX: use the executable path to find the includes files and
       libraries */
    *arg++ = "-D";
    *arg++ = "_GNU_SOURCE";
    *arg++ = "-I";
    *arg++ = inc_dir;
    *arg++ = "-o";
    *arg++ = out_filename;
    *arg++ = cfilename;
    *arg++ = "-L";
    *arg++ = lib_dir;
    *arg++ = "-ljs2c";
    *arg++ = "-lm";
    *arg = NULL;
    
    if (verbose) {
        for (arg = argv; *arg != NULL; arg++)
            printf("%s ", *arg);
        printf("\n");
    }
    
    ret = exec_cmd((char **)argv);
    unlink(cfilename);
    return ret;
}
#else
static int output_executable(const char *out_filename, const char *cfilename,
                             BOOL use_lto, BOOL verbose, const char *exename) {
    fprintf(stderr, "Executable output is not supported for this target\n");
    exit(1);
    return 0;
}
#endif


typedef enum {
    OUTPUT_C,
    OUTPUT_EXECUTABLE,
    OUTPUT_OBJECT
} OutputTypeEnum;

int main(int argc, char **argv) {
    int c, i, verbose;
    const char *out_filename, *cname;
    char cfilename[1024];
    FILE *fo;
    FILE *in_fo;
    JSRuntime *rt;
    JSContext *ctx;
    BOOL use_lto;
    int module;
    OutputTypeEnum output_type;
    char byte;
    
    out_filename = NULL;
    output_type = OUTPUT_EXECUTABLE;
    cname = "js_library";
    module = -1;
    byte_swap = FALSE;
    verbose = 0;
    use_lto = FALSE;

    for (;;) {
        c = getopt(argc, argv, "ho:cN:f:mxevM:");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
        case 'o':
            out_filename = optarg;
            break;
        case 'e':
            output_type = OUTPUT_C;
            break;
        case 'c':
            output_type = OUTPUT_OBJECT;
            break;
        case 'N':
            cname = optarg;
            break;
        case 'm':
            module = 1;
            break;
        case 'M':
            {
                char *p;
                char path[1024];
                char *cname;
                pstrcpy(path, sizeof(path), optarg);
                get_c_name(&cname);
                namelist_add(&cmodule_list, path, cname, 0);
            }
            break;
        case 'x':
            byte_swap = TRUE;
            break;
        case 'v':
            verbose++;
            break;
        default:
            break;
        }
    }

    if (optind >= argc)
        help();

    if (!out_filename) {
        if (output_type == OUTPUT_EXECUTABLE) {
            out_filename = "libout.so";
        } else if (output_type == OUTPUT_OBJECT) {
            out_filename = "out.o";
        } else {
            out_filename = "out.c";
        }
    }

    if (output_type != OUTPUT_C) {
#if defined(_WIN32) || defined(__ANDROID__)
        /* XXX: find a /tmp directory ? */
        snprintf(cfilename, sizeof(cfilename), "out%d.c", getpid());
#else
        snprintf(cfilename, sizeof(cfilename), "/tmp/out%d.c", getpid());
#endif
    } else {
        pstrcpy(cfilename, sizeof(cfilename), out_filename);
    }
    
    fo = fopen(cfilename, "w");
    if (!fo) {
        perror(cfilename);
        exit(1);
    }
    outfile = fo;
    
    rt = JS_NewRuntime();
    ctx = JS_NewContextRaw(rt);
    JS_AddIntrinsicEval(ctx);
    JS_AddIntrinsicRegExpCompiler(ctx);
    
    /* loader for ES6 modules */
    JS_SetModuleLoaderFunc(rt, NULL, jsc_module_loader, NULL);

    fprintf(fo, "/* File generated automatically by the QuickJS compiler. */\n"
            "\n"
            );
    
    fprintf(fo, init_c_header);

    for (i = optind; i < argc; i++) {
        const char *filename = argv[i];
        if (strend(filename, ".c")) {
            in_fo = fopen(filename, "r");
            if (!in_fo) {
                perror(filename);
                exit(1);
            }
            while (!feof(in_fo)) {
                fread(&byte, sizeof(char), 1, in_fo);
                fwrite(&byte, sizeof(char), 1, fo);
            }
            fclose(in_fo);
        } else {
            compile_file(ctx, fo, filename, module);
        }
    }

    fputs(init_c_template1, fo);
    fputs(cname, fo);
    fputs(init_c_template2, fo);

    for (i = 0; i < init_module_list.count; i++) {
        namelist_entry_t *e = &init_module_list.array[i];
        /* initialize the static C modules */
        
        fprintf(fo,
                "  {\n"
                "    extern JSModuleDef *js_init_module_%s(JSContext *ctx, const char *name);\n"
                "    js_init_module_%s(ctx, \"%s\");\n"
                "  }\n",
                e->short_name, e->short_name, e->name);
    }

    for (i = 0; i < cname_list.count; i++) {
        namelist_entry_t *e = &cname_list.array[i];
        fprintf(fo, "  js_std_eval_binary(ctx, %s, %s_size, %s);\n",
                e->name, e->name,
                e->flags ? "1" : "0");
    }
    fputs(init_c_template3, fo);
    fputs(cname, fo);
    fputs(init_c_template4, fo);
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    fclose(fo);

    int rc = 0;
    if (output_type == OUTPUT_EXECUTABLE) {
        rc = output_executable(out_filename, cfilename, use_lto, verbose,
                                 argv[0], 0);
    } else if (output_type == OUTPUT_OBJECT) {
        rc = output_executable(out_filename, cfilename, use_lto, verbose,
                                 argv[0], 1);
    }
    namelist_free(&cname_list);
    namelist_free(&cmodule_list);
    namelist_free(&init_module_list);
    return rc;
}
