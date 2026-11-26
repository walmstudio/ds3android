package com.ds3port.launcher;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.util.Comparator;
import java.util.stream.Stream;

public class RunDiagnosticsTest {
    public static void main(String[] args) throws Exception {
        Path docs = Files.createTempDirectory("ds3-diagnostics");
        try {
            File first = RunDiagnostics.newLog(docs.toFile());
            Files.write(first.toPath(), new byte[] { 42 });
            Files.setLastModifiedTime(first.toPath(), FileTime.fromMillis(1000));
            File frames = RunDiagnostics.frameDir(docs.toFile());
            Path original = Files.write(frames.toPath().resolve("frame_000001.png"), new byte[] { 43 });
            Path legacy = Files.write(frames.toPath().getParent().resolve("frame_000001.png"), new byte[] { 44 });
            if (!RunDiagnostics.lastLog(docs.toFile()).equals(first) || !first.getName().equals("me1exe_run.log"))
                throw new AssertionError("legacy diagnostic path remains readable");
            File next = RunDiagnostics.newLog(docs.toFile());
            File nextFrames = RunDiagnostics.frameDir(docs.toFile());
            if (first.equals(next) || frames.equals(nextFrames)) throw new AssertionError("separate files");
            if (Files.readAllBytes(first.toPath())[0] != 42 || Files.readAllBytes(original)[0] != 43 || Files.readAllBytes(legacy)[0] != 44)
                throw new AssertionError("old log and captures kept intact");
            if (!first.equals(RunDiagnostics.lastLog(docs.toFile())))
                throw new AssertionError("an empty new log must not hide the previous usable log");
            try (RunDiagnostics.LogWriter writer = new RunDiagnostics.LogWriter(next)) {
                if (!writer.available() || !writer.write("[ds3] test")) throw new AssertionError("log write");
            }
            if (!next.equals(RunDiagnostics.lastLog(docs.toFile()))) throw new AssertionError("new usable log");
            Path blockedFrames = Files.createDirectory(docs.resolve("blocked-frames"));
            Files.write(blockedFrames.resolve("me1_frames"), new byte[] { 45 });
            if (RunDiagnostics.frameDir(blockedFrames.toFile()) != null || RunDiagnostics.newLog(blockedFrames.toFile()) == null)
                throw new AssertionError("a blocked frames directory only disables capture");
            if (Files.readAllBytes(blockedFrames.resolve("me1_frames"))[0] != 45) throw new AssertionError("keep existing file");
            Path blockedDocs = Files.write(docs.resolve("blocked-docs"), new byte[] { 46 });
            if (RunDiagnostics.frameDir(blockedDocs.toFile()) != null || RunDiagnostics.newLog(blockedDocs.toFile()) != null)
                throw new AssertionError("a blocked diagnostics path disables both sinks");
            try (RunDiagnostics.LogWriter writer = new RunDiagnostics.LogWriter(blockedFrames.toFile())) {
                if (writer.available() || !writer.write("still draining")) throw new AssertionError("runner continues without a log writer");
            }
            System.out.println("ok  diagnostic runs preserve previous logs and frames; last log is discoverable");
            System.out.println("ok  blocked diagnostics paths do not require a log or frame sink");
        } finally {
            try (Stream<Path> paths = Files.walk(docs)) {
                for (Path p : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(p);
            }
        }
    }
}
