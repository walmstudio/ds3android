#include <jni.h>
#include "ds3settings.h"

JNIEXPORT jstring JNICALL Java_com_ds3port_launcher_Native_portOption(JNIEnv* env, jclass cls, jstring jd, jstring ji) {
    (void)cls;
    if (!jd || !ji) return NULL;
    const char* docs = (*env)->GetStringUTFChars(env, jd, NULL);
    if (!docs) return NULL;
    const char* id = (*env)->GetStringUTFChars(env, ji, NULL);
    char value[128];
    int found = id && ds3settings_get(docs, id, value, sizeof value);
    if (id) (*env)->ReleaseStringUTFChars(env, ji, id);
    (*env)->ReleaseStringUTFChars(env, jd, docs);
    return found ? (*env)->NewStringUTF(env, value) : NULL;
}
