#include <jni.h>

#include <mpv/client.h>

#include "jni_utils.h"
#include "globals.h"

extern "C" {
    jni_func(jint, attachSurface, jobject surface_);
    jni_func(jint, detachSurface);
};

static jobject surface;

jni_func(jint, attachSurface, jobject surface_) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return MPV_ERROR_JNI_CTX_CLOSED;

    surface = env->NewGlobalRef(surface_);
    int64_t wid = (int64_t)(intptr_t) surface;
    return mpv_set_option(lib->ctx, "wid", MPV_FORMAT_INT64, (void*) &wid);
}

jni_func(jint, detachSurface) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return MPV_ERROR_JNI_CTX_CLOSED;

    int64_t wid = 0;
    int res = mpv_set_option(lib->ctx, "wid", MPV_FORMAT_INT64, (void*) &wid);

    env->DeleteGlobalRef(surface);
    surface = NULL;
    return res;
}
