#pragma once

#include <jni.h>

#define jni_func_name(name) Java_com_lingoplay_module_mpv_MPVLib_##name
#define jni_func(return_type, name, ...) JNIEXPORT return_type JNICALL jni_func_name(name) (JNIEnv *env, jobject obj, ##__VA_ARGS__)

void init_methods_cache(JNIEnv *env);
JNIEnv *jni_get_env(const char *name);

#ifndef UTIL_EXTERN
#define UTIL_EXTERN extern
#endif

UTIL_EXTERN jclass java_Integer, java_Double, java_Boolean;
UTIL_EXTERN jmethodID java_Integer_init, java_Integer_intValue, java_Double_init, java_Double_doubleValue, java_Boolean_init, java_Boolean_booleanValue;

UTIL_EXTERN jclass android_graphics_Bitmap, android_graphics_Bitmap_Config;
UTIL_EXTERN jmethodID android_graphics_Bitmap_createBitmap;
UTIL_EXTERN jfieldID android_graphics_Bitmap_Config_ARGB_8888;

UTIL_EXTERN jclass mpv_MPVLib;
UTIL_EXTERN jclass mpv_MPVDataSource;
UTIL_EXTERN jmethodID mpv_MPVLib_eventProperty,
	mpv_MPVLib_event,
    mpv_MPVLib_eventEndFile,
	mpv_MPVLib_logMessage_SiS,
	mpv_MPVLib_openDataSource,
	mpv_MPVDataSource_size,
	mpv_MPVDataSource_read,
	mpv_MPVDataSource_seek,
	mpv_MPVDataSource_cancel,
	mpv_MPVDataSource_close
    ;
