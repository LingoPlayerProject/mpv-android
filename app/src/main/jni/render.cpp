#include <jni.h>

#include <mpv/client.h>

#include "jni_utils.h"
#include "globals.h"

extern "C" {
    jni_func(void, attachSurface, jobject surface_);
    jni_func(void, detachSurface);
};

static jobject surface;

jni_func(void, attachSurface, jobject surface_) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return;

    surface = env->NewGlobalRef(surface_);
    int64_t wid = (int64_t)(intptr_t) surface;
    mpv_set_option(lib->ctx, "wid", MPV_FORMAT_INT64, (void*) &wid);
}

jni_func(void, detachSurface) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return;

    int64_t wid = 0;
    mpv_set_option(lib->ctx, "wid", MPV_FORMAT_INT64, (void*) &wid);

    env->DeleteGlobalRef(surface);
    surface = NULL;
}
