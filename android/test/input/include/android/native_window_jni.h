#include <jni.h>
typedef struct ANativeWindow ANativeWindow;
ANativeWindow *ANativeWindow_fromSurface(JNIEnv *, jobject);
void ANativeWindow_release(ANativeWindow *);
