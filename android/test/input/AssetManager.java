package android.content.res;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

public final class AssetManager {
    private final File assets;
    public AssetManager(File assets) { this.assets = assets; }
    public String[] list(String path) { return new File(assets, path).list(); }
    public InputStream open(String path) throws IOException { return new FileInputStream(new File(assets, path)); }
}
