#include <jni.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <locale.h>
#include <atomic>

#include <mpv/client.h>

extern "C" {
    #include <libavcodec/jni.h>
}

#include "log.h"
#include "jni_utils.h"
#include "event.h"
#include "data_source.h"

#define ARRAYLEN(a) (sizeof(a)/sizeof(a[0]))

extern "C" {
    jni_func(jlong, create, jobject appctx);
    jni_func(void, init);
    jni_func(void, destroy);

    jni_func(void, command, jobjectArray jarray);
};

static void prepare_environment(JNIEnv *env, jobject appctx) {
    setlocale(LC_NUMERIC, "C");

    JavaVM *g_vm;
    if (!env->GetJavaVM(&g_vm) && g_vm)
        av_jni_set_java_vm(g_vm, NULL);
}

jni_func(jlong, create, jobject appctx) {
    prepare_environment(env, appctx);

    mpv_handle *ctx = mpv_create();
    if (!ctx)
        die("context init failed");

    mpv_stream_cb_add_ro(ctx, "datasource", NULL, mpv_open_data_source_fn);
    jobject ref = env->NewGlobalRef(obj);
    mpv_lib *lib;
    if (ref == NULL) {
        goto error;
    }
    lib = (mpv_lib *) malloc(sizeof(mpv_lib));
    if (lib == NULL) {
        goto error;
    }
    lib->ctx = ctx;
    lib->obj = ref;
    mpv_set_wakeup_callback(ctx, event_enqueue_cb, lib);

    // use terminal log level but request verbose messages
    // this way --msg-level can be used to adjust later
    mpv_request_log_messages(ctx, "debug");
    mpv_set_option_string(ctx, "msg-level", "all=v");

    return (jlong) lib;
error:
    if (ref) {
        env->DeleteGlobalRef(ref);
    }
    if (ctx) {
        mpv_terminate_destroy(ctx);
    }
    die("context init failed");
    return -1;
}

jni_func(void, init) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib)
        return;

    if (mpv_initialize(lib->ctx) < 0)
        die("mpv init failed");

#ifdef __aarch64__
    ALOGV("You're using the 64-bit build of mpv!");
#endif
}

jni_func(void, destroy) {
    ALOGV("mpv_lib destroy called");
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib)
        return;

    env->SetLongField(obj, mpv_MPVLib_handler, (jlong) 0);
    mpv_set_wakeup_callback(lib->ctx, event_enqueue_cb, NULL); // stop new events
    destroy_events(lib); // before mpv_terminate_destroy must stop calling wait_event, or else it will crash
    mpv_terminate_destroy(lib->ctx);
    env->DeleteGlobalRef(lib->obj);
    free(lib);
    ALOGV("mpv_lib destroyed");
}

jni_func(void, command, jobjectArray jarray) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib)
        return;

    const char *arguments[128] = { 0 };
    int len = env->GetArrayLength(jarray);
    if (len >= ARRAYLEN(arguments))
        die("Cannot run command: too many arguments");

    for (int i = 0; i < len; ++i)
        arguments[i] = env->GetStringUTFChars((jstring)env->GetObjectArrayElement(jarray, i), NULL);

    int result = mpv_command(lib->ctx, arguments);
    if (result)
        ALOGE("mpv_command error [%s] -> %s \n", len > 0 ? arguments[0] : "", mpv_error_string(result));

    for (int i = 0; i < len; ++i)
        env->ReleaseStringUTFChars((jstring)env->GetObjectArrayElement(jarray, i), arguments[i]);
}

