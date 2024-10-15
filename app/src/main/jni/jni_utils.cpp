#define UTIL_EXTERN
#include "jni_utils.h"
#include "log.h"
#include "event.h"

#include <jni.h>
#include <stdlib.h>
#include <pthread.h>

#define VLC_JNI_VERSION JNI_VERSION_1_6

JNIEnv *jni_get_env(const char *name);

/* Pointer to the Java virtual machine
 * Note: It's okay to use a static variable for the VM pointer since there
 * can only be one instance of this shared library in a single VM
 */
static JavaVM *myVm;

static pthread_key_t jni_env_key;

/* This function is called when a thread attached to the Java VM is canceled or
 * exited */
static void jni_detach_thread(void *data)
{
    //JNIEnv *env = data;
    myVm->DetachCurrentThread();
}

JNIEnv *jni_get_env(const char *name)
{
    JNIEnv *env;

    env =  (JNIEnv *) pthread_getspecific(jni_env_key);
    if (env == NULL) {
        /* if GetEnv returns JNI_OK, the thread is already attached to the
         * JavaVM, so we are already in a java thread, and we don't have to
         * setup any destroy callbacks */
        if (myVm->GetEnv((void **)&env, VLC_JNI_VERSION) != JNI_OK) {
            /* attach the thread to the Java VM */
            JavaVMAttachArgs args;
            jint result;

            args.version = VLC_JNI_VERSION;
            args.name = name;
            args.group = NULL;

            if (myVm->AttachCurrentThread(&env, &args) != JNI_OK) {
                ALOGE("jni get env failed: unable to AttachCurrentThread");
                return NULL;
            }

            /* Set the attached env to the thread-specific data area (TSD) */
            if (pthread_setspecific(jni_env_key, env) != 0)
            {
                ALOGE("jni get env failed: unable to pthread_setspecific");
                myVm->DetachCurrentThread();
                return NULL;
            }
        }
    }

    return env;
}

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    ALOGV("mpv jni load");

    JNIEnv *env = NULL;
    // Keep a reference on the Java VM.
    myVm = vm;

    if (vm->GetEnv((void**) &env, VLC_JNI_VERSION) != JNI_OK) {
        return -1;
    }

    /* Create a TSD area and setup a destroy callback when a thread that
     * previously set the jni_env_key is canceled or exited */
    if (pthread_key_create(&jni_env_key, jni_detach_thread) != 0) {
        return -1;
    }

    init_methods_cache(env);

    start_event_thread();

    return VLC_JNI_VERSION;
}

void JNI_OnUnload(JavaVM* vm, void* reserved)
{
    stop_event_thread();

    pthread_key_delete(jni_env_key);

    JNIEnv* env = NULL;
    if (vm->GetEnv((void**) &env, VLC_JNI_VERSION) != JNI_OK) {
        return;
    }

    env->DeleteGlobalRef(java_Integer);
    env->DeleteGlobalRef(java_Double);
    env->DeleteGlobalRef(java_Boolean);
    env->DeleteGlobalRef(android_graphics_Bitmap);
    env->DeleteGlobalRef(android_graphics_Bitmap_Config);
    env->DeleteGlobalRef(mpv_MPVLib);
    env->DeleteGlobalRef(mpv_MPVDataSource);
    env->DeleteGlobalRef(java_RuntimeException);
}

mpv_lib* get_mpv_lib(JNIEnv *env, jobject jobject) {
    jlong r = env->GetLongField(jobject, mpv_MPVLib_handler);
    if (r == 0) {
        ALOGE("MPVLib is destroyed");
        return NULL;
    }
    return (mpv_lib*) ((void*) r);
}

jstring SafeNewStringUTF(JNIEnv *env, const char* bytes) {
    jstring res = env->NewStringUTF(bytes);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ALOGE("NewStringUTF failed for %s \n", bytes);
        return NULL;
    }
    return res;
}

// Apparently it's considered slow to FindClass and GetMethodID every time we need them,
// so let's have a nice cache here.

void init_methods_cache(JNIEnv *env)
{
#define FIND_CLASS(name) reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass(name)))
    java_Integer = FIND_CLASS("java/lang/Integer");
    java_Integer_init = env->GetMethodID(java_Integer, "<init>", "(I)V");
    java_Integer_intValue = env->GetMethodID(java_Integer, "intValue", "()I");
    java_Double = FIND_CLASS("java/lang/Double");
    java_Double_init = env->GetMethodID(java_Double, "<init>", "(D)V");
    java_Double_doubleValue = env->GetMethodID(java_Double, "doubleValue", "()D");
    java_Boolean = FIND_CLASS("java/lang/Boolean");
    java_Boolean_init = env->GetMethodID(java_Boolean, "<init>", "(Z)V");
    java_Boolean_booleanValue = env->GetMethodID(java_Boolean, "booleanValue", "()Z");
    java_RuntimeException = FIND_CLASS("java/lang/RuntimeException");

    android_graphics_Bitmap = FIND_CLASS("android/graphics/Bitmap");
    // createBitmap(int[], int, int, android.graphics.Bitmap$Config)
    android_graphics_Bitmap_createBitmap = env->GetStaticMethodID(android_graphics_Bitmap, "createBitmap", "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    android_graphics_Bitmap_Config = FIND_CLASS("android/graphics/Bitmap$Config");
    // static final android.graphics.Bitmap$Config ARGB_8888
    android_graphics_Bitmap_Config_ARGB_8888 = env->GetStaticFieldID(android_graphics_Bitmap_Config, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");

    mpv_MPVLib = FIND_CLASS("com/lingoplay/module/mpv/MPVLib");
    mpv_MPVLib_handler = env->GetFieldID(mpv_MPVLib, "handler", "J");
    mpv_MPVLib_event  = env->GetMethodID(mpv_MPVLib, "event", "(IJ)V");
    mpv_MPVLib_eventEndFile  = env->GetMethodID(mpv_MPVLib, "eventEndFile", "(II)V");
    mpv_MPVLib_eventProperty  = env->GetMethodID(mpv_MPVLib, "eventProperty", "(Ljava/lang/String;IJJZDLjava/lang/String;)V");
    mpv_MPVLib_logMessage_SiS = env->GetMethodID(mpv_MPVLib, "logMessage", "(Ljava/lang/String;ILjava/lang/String;)V"); // logMessage(String, int, String)
    mpv_MPVLib_openDataSource = env->GetStaticMethodID(mpv_MPVLib, "openDataSource", "(Ljava/lang/String;)Lcom/lingoplay/module/mpv/MPVDataSource;");

    mpv_MPVDataSource = FIND_CLASS("com/lingoplay/module/mpv/MPVDataSource");
    mpv_MPVDataSource_size  = env->GetMethodID(mpv_MPVDataSource, "size", "()J");
    mpv_MPVDataSource_read  = env->GetMethodID(mpv_MPVDataSource, "read", "([BI)I");
    mpv_MPVDataSource_seek  = env->GetMethodID(mpv_MPVDataSource, "seek", "(J)V");
    mpv_MPVDataSource_cancel  = env->GetMethodID(mpv_MPVDataSource, "cancel", "()V");
    mpv_MPVDataSource_close  = env->GetMethodID(mpv_MPVDataSource, "close", "()V");
#undef FIND_CLASS

}
