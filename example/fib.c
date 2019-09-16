#include <inttypes.h>

int64_t js_fib(int64_t x) {
    int64_t rc;

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue fib_func = JS_GetPropertyStr(ctx, global_obj, "fib");

    JSValueConst args[1];
    args[0] = JS_NewInt64(ctx, x);

    JSValue val = JS_Call(ctx, fib_func, global_obj, 1, args);
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        rc = -1;
        goto cleanup;
    }

    if (JS_ToInt64(ctx, &rc, val)) {
        rc = -1;
    }

    JS_FreeValue(ctx, val);
cleanup:
    JS_FreeValue(ctx, fib_func);
    JS_FreeValue(ctx, global_obj);
    JS_FreeValue(ctx, args[0]);
    return rc;
}
