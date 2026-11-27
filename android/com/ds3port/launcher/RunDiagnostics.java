package com.ds3port.launcher;

import java.io.File;
import java.io.IOException;
import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Keep the existing diagnostic location and names, without reusing a previous run's files. */
final class RunDiagnostics {
    private static final String LOG_BASE = "me1exe_run";
    private static final String LOG_EXT = ".log";

    private RunDiagnostics() {}

    static File newLog(File docs) {
        try {
            Files.createDirectories(docs.toPath());
            File legacy = new File(docs, LOG_BASE + LOG_EXT);
            if (legacy.createNewFile()) return legacy;
            return File.createTempFile(LOG_BASE + "-", LOG_EXT, docs);
        } catch (IOException | SecurityException ignored) { return null; }
    }

    static File frameDir(File docs) {
        File root = new File(docs, "me1_frames");
        try {
            Files.createDirectories(root.toPath());
            return Files.createTempDirectory(root.toPath(), "run-").toFile();
        } catch (IOException | SecurityException ignored) { return null; }
    }

    static File lastLog(File docs) {
        File last = null;
        File[] files = docs.listFiles((dir, name) -> name.equals(LOG_BASE + LOG_EXT)
                || (name.startsWith(LOG_BASE + "-") && name.endsWith(LOG_EXT)));
        if (files != null) for (File file : files)
            if (file.isFile() && file.canRead() && file.length() > 0
                    && (last == null || file.lastModified() > last.lastModified())) last = file;
        return last;
    }

    /** Loss of optional disk output must not stop draining the runner's stdout. */
    static final class LogWriter implements AutoCloseable {
        private BufferedWriter writer;

        LogWriter(File log) {
            if (log != null) try {
                writer = Files.newBufferedWriter(log.toPath(), StandardCharsets.UTF_8);
            } catch (IOException | SecurityException ignored) { }
        }

        boolean available() { return writer != null; }

        boolean write(String line) {
            if (writer == null) return true;
            try {
                writer.write(line);
                writer.newLine();
                return true;
            } catch (IOException ignored) {
                close();
                return false;
            }
        }

        @Override public void close() {
            if (writer != null) {
                try { writer.close(); } catch (IOException ignored) { }
                writer = null;
            }
        }
    }
}
