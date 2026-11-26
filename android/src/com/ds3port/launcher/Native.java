package com.ds3port.launcher;

/** JNI bridge for the allow-listed DS3 port INI and its read-only legacy fallback. */
public final class Native {
    static { System.loadLibrary("ds3port"); }
    private Native() {}
    public static native String portOption(String docsDir, String id);
}
