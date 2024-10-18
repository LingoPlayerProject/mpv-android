#include <jni.h>

#include <mpv/client.h>

#include "globals.h"
#include "jni_utils.h"
#include "log.h"
#include "blocking_queue.h"
#include <pthread.h>

#define MPV_EVENT_THREAD "mpv-event"

static blocking_queue_t *queue;
static pthread_t thread_id;
static pthread_mutex_t lock;

static void sendPropertyUpdateToJava(JNIEnv *env, jobject obj, uint64_t opaque_data, mpv_event_property *prop) {
    jstring jprop = SafeNewStringUTF(env, prop->name);
    jlong long_val = 0;
    jboolean bool_val = false;
    jdouble double_val = false;
    jstring str_val = NULL;

    switch (prop->format) {
        case MPV_FORMAT_NONE:
            break;
        case MPV_FORMAT_FLAG:
            bool_val = (jboolean)(*(int *) prop->data != 0);
            break;
        case MPV_FORMAT_INT64:
            long_val = (jlong) * (int64_t *) prop->data;
            break;
        case MPV_FORMAT_DOUBLE:
            double_val = (jdouble) * (double *) prop->data;
            break;
        case MPV_FORMAT_STRING:
            str_val = SafeNewStringUTF(env, *(const char **) prop->data);
            break;
        default:
            ALOGV("sendPropertyUpdateToJava: Unknown property update format received in callback: %d!",
                  prop->format);
            break;
    }
    env->CallVoidMethod(obj, mpv_MPVLib_eventProperty, jprop,
                        (jint) prop->format, (jlong) opaque_data,
                        long_val, bool_val, double_val, str_val);
    if (env->ExceptionCheck()) {
        ALOGE("callback eventProperty '%s' got exception! \n", prop->name);
        env->ExceptionClear();
    }
    if (jprop)
        env->DeleteLocalRef(jprop);
    if (str_val)
        env->DeleteLocalRef(str_val);
}

static void sendLogMessageToJava(JNIEnv *env, jobject obj, mpv_event_log_message *msg) {
    jstring jprefix = SafeNewStringUTF(env, msg->prefix);
    jstring jtext = SafeNewStringUTF(env, msg->text);

    env->CallVoidMethod(obj, mpv_MPVLib_logMessage_SiS,
                        jprefix, (jint) msg->log_level, jtext);

    if (jprefix)
        env->DeleteLocalRef(jprefix);
    if (jtext)
        env->DeleteLocalRef(jtext);
}

static void *dispatcher_thread(void *arg) {
    JNIEnv *env = jni_get_env(MPV_EVENT_THREAD);
    if (!env) {
        ALOGE("failed to acquire java env");
        exit(-1);
    }

    while (1) {
        blocking_queue_optional_data res = blocking_queue_peek(queue, true);
        if (!res.has_data)
            continue;

        pthread_mutex_lock(&lock);
        res = blocking_queue_peek(queue, true);
        if (!res.has_data) {
            pthread_mutex_unlock(&lock);
            continue;
        }
        if (res.data == NULL) {
            blocking_queue_pop(queue);
            pthread_mutex_unlock(&lock);
            continue;
        }
        mpv_event *mp_event;
        mpv_event_property *mp_property = NULL;
        mpv_event_log_message *msg = NULL;
        mpv_lib *lib = (mpv_lib *) res.data;
        jobject obj = lib->obj;
        mp_event = mpv_wait_event(lib->ctx, 0);
        if (mp_event->event_id == MPV_EVENT_NONE) {
            blocking_queue_pop(queue);
            pthread_mutex_unlock(&lock);
            continue;
        }

        switch (mp_event->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                msg = (mpv_event_log_message *) mp_event->data;
                ALOGV("[%s:%s] %s", msg->prefix, msg->level, msg->text);
                sendLogMessageToJava(env, obj, msg);
                break;
            case MPV_EVENT_PROPERTY_CHANGE:
                mp_property = (mpv_event_property *) mp_event->data;
                sendPropertyUpdateToJava(env, obj, mp_event->reply_userdata, mp_property);
                break;
            case MPV_EVENT_END_FILE:
                jint reason;
                reason = (jint)((mpv_event_end_file *) mp_event->data)->reason;
                //ALOGV("event: %s\n", mpv_event_name(mp_event->event_id));
                env->CallVoidMethod(obj, mpv_MPVLib_eventEndFile, mp_event->event_id,
                                    reason);
                break;
            default:
                //ALOGV("event: %s\n", mpv_event_name(mp_event->event_id));
                env->CallVoidMethod(obj, mpv_MPVLib_event, mp_event->event_id,
                                    (jlong) mp_event->reply_userdata);
                break;
        }
        // copy mpv_event or lock until mpv_event is not used
        pthread_mutex_unlock(&lock);
    }

    ALOGV("Native destroyed, event thread exited!");
    return NULL;
}

void destroy_events(mpv_lib *lib) {
    pthread_mutex_lock(&lock);
    blocking_queue_replace_to_null(queue, lib);
    pthread_mutex_unlock(&lock);
}

void event_enqueue_cb(void *d) {
    if (!d) return;

    if (blocking_queue_size(queue) > 1000) {
        ALOGE("too many queued events");
        return;
    }
    if (blocking_queue_push(queue, d)) {
        ALOGE("blocking_queue_push failed");
    }
}

/**
 * Called when JNILoad. The callback in mpv_set_wakeup_callback is not allowed to do
 * anything, including mpv_wait_event, so we need a dispatcher thread.
 */
void start_event_thread() {
    queue = blocking_queue_create();
    if (queue == NULL) {
        ALOGE("start_event_thread init failed");
        return;
    }
    if(pthread_create(&thread_id, NULL, dispatcher_thread, NULL)) {
        ALOGE("start_event_thread pthread_create failed");
        return;
    }
    pthread_mutex_init(&lock, NULL);
    ALOGV("event thread started");
}

/**
 * Called when JNIUnload, seems never happen.
 * Even if it happens, there is no need to stop eventThread,
 * as long as you do not use the destroyed mpv_handler
 */
void stop_event_thread() {
}
