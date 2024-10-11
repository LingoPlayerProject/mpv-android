#include <jni.h>

#include <mpv/client.h>

#include "globals.h"
#include "jni_utils.h"
#include "log.h"
#define MPV_EVENT_THREAD "mpv-event"

static void sendPropertyUpdateToJava(JNIEnv *env, uint64_t opaque_data, mpv_event_property *prop) {
    jstring jprop = env->NewStringUTF(prop->name);
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
            str_val = env->NewStringUTF(*(const char **) prop->data);
            break;
        default:
            ALOGV("sendPropertyUpdateToJava: Unknown property update format received in callback: %d!",
                  prop->format);
            break;
    }
    env->CallStaticVoidMethod(mpv_MPVLib, mpv_MPVLib_eventProperty, jprop,
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


static inline bool invalid_utf8(unsigned char c) {
    return c == 0xc0 || c == 0xc1 || c >= 0xf5;
}

static void sendLogMessageToJava(JNIEnv *env, mpv_event_log_message *msg) {
    // filter the most obvious cases of invalid utf-8
    int invalid = 0;
    for (int i = 0; msg->text[i]; i++)
        invalid |= invalid_utf8((unsigned char) msg->text[i]);
    if (invalid)
        return;

    jstring jprefix = env->NewStringUTF(msg->prefix);
    jstring jtext = env->NewStringUTF(msg->text);

    env->CallStaticVoidMethod(mpv_MPVLib, mpv_MPVLib_logMessage_SiS,
                              jprefix, (jint) msg->log_level, jtext);

    if (jprefix)
        env->DeleteLocalRef(jprefix);
    if (jtext)
        env->DeleteLocalRef(jtext);
}

void *event_thread(void *arg) {
    JNIEnv *env = jni_get_env(MPV_EVENT_THREAD);
    if (!env)
        die("failed to acquire java env");

    while (1) {
        mpv_event *mp_event;
        mpv_event_property *mp_property = NULL;
        mpv_event_log_message *msg = NULL;

        mp_event = mpv_wait_event(g_mpv, -1.0);

        if (destroyed) {
            break;
        }

        if (mp_event->event_id == MPV_EVENT_NONE)
            continue;

        switch (mp_event->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                msg = (mpv_event_log_message*)mp_event->data;
                ALOGV("[%s:%s] %s", msg->prefix, msg->level, msg->text);
                sendLogMessageToJava(env, msg);
                break;
            case MPV_EVENT_PROPERTY_CHANGE:
                mp_property = (mpv_event_property*)mp_event->data;
                sendPropertyUpdateToJava(env, mp_event->reply_userdata, mp_property);
                break;
            case MPV_EVENT_END_FILE:
                jint reason;
                reason = (jint) ((mpv_event_end_file*)mp_event->data)->reason;
                ALOGV("event: %s\n", mpv_event_name(mp_event->event_id));
                env->CallStaticVoidMethod(mpv_MPVLib, mpv_MPVLib_eventEndFile, mp_event->event_id, reason);
                break;
            case MPV_EVENT_SHUTDOWN:
                destroyed = true; 
                ALOGV("Received MPV_EVENT_SHUTDOWN, Mark destroyed");
            default:
                ALOGV("event: %s\n", mpv_event_name(mp_event->event_id));
                env->CallStaticVoidMethod(mpv_MPVLib, mpv_MPVLib_event, mp_event->event_id, (jlong) mp_event->reply_userdata);
                break;
        }
    }

    ALOGV("Native destroyed, event thread exited!");
    return NULL;
}
