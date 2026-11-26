package android.content;

import android.content.res.AssetManager;
import java.io.File;

public abstract class Context {
    public static final int MODE_MULTI_PROCESS = 4;
    public abstract AssetManager getAssets();
    public abstract File getFilesDir();
    public abstract SharedPreferences getSharedPreferences(String name, int mode);
    public ContentResolver getContentResolver() { throw new UnsupportedOperationException(); }
}
