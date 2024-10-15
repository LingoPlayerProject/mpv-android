#include <jni.h>
#include <stdlib.h>

#include <mpv/client.h>

#include "jni_utils.h"
#include "log.h"
#include "globals.h"

extern "C" {
    jni_func(jint, setOptionString, jstring option, jstring value);
    jni_func(jint, setOptionStringOfArray, jstring option, jstring value);

    jni_func(jobject, getPropertyInt, jstring property);
    jni_func(void, setPropertyInt, jstring property, jobject value);
    jni_func(jobject, getPropertyDouble, jstring property);
    jni_func(void, setPropertyDouble, jstring property, jobject value);
    jni_func(jobject, getPropertyBoolean, jstring property);
    jni_func(void, setPropertyBoolean, jstring property, jobject value);
    jni_func(jstring, getPropertyString, jstring jproperty);
    jni_func(void, setPropertyString, jstring jproperty, jstring jvalue);

    jni_func(void, observeProperty, jstring property, jint format, jlong opaque_data);
    jni_func(void, unobserveProperty, jlong opaque_data);
}

jni_func(jint, setOptionString, jstring joption, jstring jvalue) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return -1;

    const char *option = env->GetStringUTFChars(joption, NULL);
    const char *value = env->GetStringUTFChars(jvalue, NULL);

    int result = mpv_set_option_string(lib->ctx, option, value);
    if (result < 0)
        ALOGE("mpv_set_option_string(%s) returned error %s", option, mpv_error_string(result));

    env->ReleaseStringUTFChars(joption, option);
    env->ReleaseStringUTFChars(jvalue, value);

    return result;
}

// avoid url ':' split issue
jni_func(jint, setOptionStringArraySingle, jstring joption, jstring jvalue) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return -1;

    const char *option = env->GetStringUTFChars(joption, NULL);
    char *value = (char *) env->GetStringUTFChars(jvalue, NULL);

    // mpv_node node = {
    //     .u.list = &(mpv_node_list) {
    //         .num = 1,
    //         .values = &(mpv_node) {
    //             .u.string = value,
    //             .format = MPV_FORMAT_STRING,
    //         },
    //     },
    //     .format = MPV_FORMAT_NODE_ARRAY,
    // };

    mpv_node_list node_list;
    mpv_node node_value;

    node_value.u.string = value;
    node_value.format = MPV_FORMAT_STRING;

    node_list.num = 1;
    node_list.values = &node_value;
    node_list.keys = NULL;
    
    mpv_node node;
    node.u.list = &node_list;
    node.format = MPV_FORMAT_NODE_ARRAY;

    int result = mpv_set_option(lib->ctx, option, MPV_FORMAT_NODE, &node);
    if (result < 0)
        ALOGE("mpv_set_option(%s) returned error %s", option, mpv_error_string(result));

    env->ReleaseStringUTFChars(joption, option);
    env->ReleaseStringUTFChars(jvalue, value);

    return result;
}

static int common_get_property(JNIEnv *env, jobject obj, jstring jproperty, mpv_format format, void *output) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return -1;

    const char *prop = env->GetStringUTFChars(jproperty, NULL);
    int result = mpv_get_property(lib->ctx, prop, format, output);
    if (result < 0)
        ALOGV("mpv_get_property(%s) format %d returned error %s", prop, format, mpv_error_string(result));
    env->ReleaseStringUTFChars(jproperty, prop);

    return result;
}

static int common_set_property(JNIEnv *env, jobject obj, jstring jproperty, mpv_format format, void *value) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return -1;

    const char *prop = env->GetStringUTFChars(jproperty, NULL);
    int result = mpv_set_property(lib->ctx, prop, format, value);
    if (result < 0)
        ALOGE("mpv_set_property(%s, %p) format %d returned error %s", prop, value, format, mpv_error_string(result));
    env->ReleaseStringUTFChars(jproperty, prop);

    return result;
}

jni_func(jobject, getPropertyInt, jstring jproperty) {
    int64_t value = 0;
    if (common_get_property(env, obj, jproperty, MPV_FORMAT_INT64, &value) < 0)
        return NULL;
    return env->NewObject(java_Integer, java_Integer_init, (jint)value);
}

jni_func(jobject, getPropertyDouble, jstring jproperty) {
    double value = 0;
    if (common_get_property(env, obj, jproperty, MPV_FORMAT_DOUBLE, &value) < 0)
        return NULL;
    return env->NewObject(java_Double, java_Double_init, (jdouble)value);
}

jni_func(jobject, getPropertyBoolean, jstring jproperty) {
    int value = 0;
    if (common_get_property(env, obj, jproperty, MPV_FORMAT_FLAG, &value) < 0)
        return NULL;
    return env->NewObject(java_Boolean, java_Boolean_init, (jboolean)value);
}

jni_func(jstring, getPropertyString, jstring jproperty) {
    char *value;
    if (common_get_property(env, obj, jproperty, MPV_FORMAT_STRING, &value) < 0)
        return NULL;
    jstring jvalue = env->NewStringUTF(value);
    mpv_free(value);
    return jvalue;
}

jni_func(void, setPropertyInt, jstring jproperty, jobject jvalue) {
    int64_t value = env->CallIntMethod(jvalue, java_Integer_intValue);
    common_set_property(env, obj, jproperty, MPV_FORMAT_INT64, &value);
}

jni_func(void, setPropertyDouble, jstring jproperty, jobject jvalue) {
    double value = env->CallDoubleMethod(jvalue, java_Double_doubleValue);
    common_set_property(env, obj, jproperty, MPV_FORMAT_DOUBLE, &value);
}

jni_func(void, setPropertyBoolean, jstring jproperty, jobject jvalue) {
    int value = env->CallBooleanMethod(jvalue, java_Boolean_booleanValue);
    common_set_property(env, obj, jproperty, MPV_FORMAT_FLAG, &value);
}

jni_func(void, setPropertyString, jstring jproperty, jstring jvalue) {
    const char *value = env->GetStringUTFChars(jvalue, NULL);
    common_set_property(env, obj, jproperty, MPV_FORMAT_STRING, &value);
    env->ReleaseStringUTFChars(jvalue, value);
}

jni_func(void, observeProperty, jstring property, jint format, jlong opaque_data) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return;

    const char *prop = env->GetStringUTFChars(property, NULL);
    int result = mpv_observe_property(lib->ctx, opaque_data, prop, (mpv_format)format);
    if (result < 0)
        ALOGE("mpv_observe_property(%s) format %d returned error %s", prop, format, mpv_error_string(result));
    env->ReleaseStringUTFChars(property, prop);
}

jni_func(void, unobserveProperty, jlong opaque_data) {
    mpv_lib* lib = get_mpv_lib(env, obj);
    if (!lib) return;

    int result = mpv_unobserve_property(lib->ctx, opaque_data);
    if (result <= 0)
        ALOGV("unobserveProperty has no effect!");
}