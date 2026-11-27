package com.ds3port.launcher;

import java.io.File;
import java.io.IOException;
import java.util.Map;

final class Ds3VideoOptions {
    static final String MODE = "ds3VideoMode", ICD = "ds3VideoIcd", ZIP_NAME = "ds3VideoZipName", ZIP_ID = "ds3VideoZipId";

    static void putEnv(Map<String,String> env, String mode, String id, File privateDir) throws IOException {
        env.remove("DS3_VK_DRIVER");
        if ("zip".equals(mode)) throw new IOException("Selected ZIP must be verified before launch.");
        boolean custom = "custom".equals(mode);
        env.put("DS3_RENDERER", custom || "vulkan".equals(mode) ? "vulkan" : "gles");
        if (custom) {
            if (id == null || !id.matches("icd-[a-f0-9-]{36}\\.so")) throw new IOException("No imported Vulkan ICD; select system Vulkan or GLES.");
            File file = new File(privateDir.getCanonicalFile(), id);
            if (!file.equals(file.getCanonicalFile()) || !file.isFile()) throw new IOException("Imported Vulkan ICD missing; reimport it or select GLES.");
            env.put("DS3_VK_DRIVER", file.getPath());
        }
    }

    static void putZipEnv(Map<String,String> env, File gameDir, File privateDir, String name, String id) throws IOException {
        env.remove("DS3_VK_DRIVER");
        File file = GpuDrivers.selectedZip(gameDir, privateDir, name, id);
        env.put("DS3_RENDERER", "vulkan");
        env.put("DS3_VK_DRIVER", file.getPath());
    }
}
