#include <jni.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <locale.h>
#include <atomic>

#include <mpv/client.h>

#include <pthread.h>

extern "C" {
    #include <libavcodec/jni.h>
}

#include "log.h"
#include "jni_utils.h"
#include "event.h"
#include "data_source.h"

#define ARRAYLEN(a) (sizeof(a)/sizeof(a[0]))

extern "C" {
    jni_func(void, create, jobject appctx);
    jni_func(void, init);
    jni_func(void, destroy);

    jni_func(void, command, jobjectArray jarray);
};

mpv_handle *g_mpv;
std::atomic<bool> destroyed(false);

static pthread_t event_thread_id;

static void prepare_environment(JNIEnv *env, jobject appctx) {
    setlocale(LC_NUMERIC, "C");

    JavaVM *g_vm;
    if (!env->GetJavaVM(&g_vm) && g_vm)
        av_jni_set_java_vm(g_vm, NULL);
}

jni_func(void, create, jobject appctx) {
    prepare_environment(env, appctx);

    if (g_mpv)
        die("mpv is already initialized");

    g_mpv = mpv_create();
    if (!g_mpv)
        die("context init failed");

    mpv_stream_cb_add_ro(g_mpv, "datasource", NULL, mpv_open_data_source_fn);

    // use terminal log level but request verbose messages
    // this way --msg-level can be used to adjust later
    mpv_request_log_messages(g_mpv, "debug");
    mpv_set_option_string(g_mpv, "msg-level", "all=v");
}

jni_func(void, init) {
    if (!g_mpv)
        die("mpv is not created");

    if (mpv_initialize(g_mpv) < 0)
        die("mpv init failed");

#ifdef __aarch64__
    ALOGV("You're using the 64-bit build of mpv!");
#endif

    destroyed = false;
    pthread_create(&event_thread_id, NULL, event_thread, NULL);
}

jni_func(void, destroy) {
    if (!g_mpv)
        die("mpv destroy called but it's already destroyed");
    if (destroyed)
        return;
    // poke event thread and wait for it to exit
    destroyed = true;
    ALOGV("Native destroy..");
    mpv_wakeup(g_mpv);
    int r = pthread_join(event_thread_id, NULL);
    if (r != 0) {
        ALOGV("pthread_join failed: %s\n", strerror(r));
    } else {
        ALOGV("Native destroy event_thread joined");
    }

    mpv_terminate_destroy(g_mpv);
    g_mpv = NULL;
    ALOGV("Native destroy mpv_handle destroyed");
}

jni_func(void, command, jobjectArray jarray) {
    const char *arguments[128] = { 0 };
    int len = env->GetArrayLength(jarray);
    if (!g_mpv)
        die("Cannot run command: mpv is not initialized");
    if (len >= ARRAYLEN(arguments))
        die("Cannot run command: too many arguments");

    for (int i = 0; i < len; ++i)
        arguments[i] = env->GetStringUTFChars((jstring)env->GetObjectArrayElement(jarray, i), NULL);

    int r = mpv_command(g_mpv, arguments);
    if (r) {
        ALOGE("mpv_command error [%s] -> %s \n", len > 0 ? arguments[0] : "", mpv_error_string(result));
    }

    for (int i = 0; i < len; ++i)
        env->ReleaseStringUTFChars((jstring)env->GetObjectArrayElement(jarray, i), arguments[i]);
}

