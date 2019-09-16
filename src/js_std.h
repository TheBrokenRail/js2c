#include "quickjs.h"

void js_std_dump_error(JSContext *);

void js_std_init(JSContext *);

uint8_t *js_load_file(JSContext *, size_t *, const char *);

void js_std_eval_binary(JSContext *, const uint8_t *, size_t, int);

int js_module_set_import_meta(JSContext *, JSValueConst, JS_BOOL, JS_BOOL);
