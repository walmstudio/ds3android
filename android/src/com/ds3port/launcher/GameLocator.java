package com.ds3port.launcher;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Finds the stock DS3 root and chooses the Documents path passed to the port.
 *
 * Search order: manual choice → <storage root>/deadspace3 → other root folders named "Dead Space 3*" (any case/spaces) on
 * internal storage and SD cards → the old app-private folder (Android/data/…/files/deadspace3). Each candidate may hold
 * the game directly or one level deeper. No installation or migration is performed here.
 */
public final class GameLocator {
    public static final String DEFAULT_NAME = "deadspace3";
    // tools/profiles/ds3_dcc26a3d.json: the executable used to generate the guest.
    private static final String EXE_SHA256 = "dcc26a3d0afc1af232cb8e6d8c9192ff0de9a43ab51990be6756dd2993a63ba2";
    public enum Status { SUPPORTED, MISSING_FILES, UNSUPPORTED_EXE, UNREADABLE_EXE }
    public static final class Compatibility {
        public final Status status;
        public final List<String> missing;
        Compatibility(Status status, List<String> missing) {
            this.status = status;
            this.missing = java.util.Collections.unmodifiableList(missing);
        }
    }
    public Compatibility compatibility;
    public File gameDir, docsDir;
    public boolean found, manual, legacyPrivate;
    public final List<File> searched = new ArrayList<>();

    /** roots[0] = primary shared storage (/sdcard), the rest = SD cards; priv = app-specific external dir */
    public static GameLocator locate(List<File> roots, File priv, String manualPath) {
        return locate(roots, priv, manualPath, EXE_SHA256);
    }

    static GameLocator locate(List<File> roots, File priv, String manualPath, String expectedHash) {
        GameLocator L = new GameLocator();
        File primary = roots.isEmpty() ? null : roots.get(0);
        if (manualPath != null && !manualPath.trim().isEmpty()) {
            L.manual = true;
            L.inspect(new File(manualPath.trim()), expectedHash);
        } else {
            outer:
            for (File r : roots) for (File c : candidates(r)) {
                if (L.inspect(c, expectedHash)) break outer;
            }
            if (!L.found && priv != null) {
                if (L.inspect(new File(priv, DEFAULT_NAME), expectedHash)) L.legacyPrivate = true;
            }
            if (L.gameDir == null) L.inspect(new File(primary != null ? primary : priv, DEFAULT_NAME), expectedHash);
        }
        File preferred = new File(L.gameDir, "Documents");
        File shared = primary == null ? null : new File(new File(primary, DEFAULT_NAME), "Documents");
        L.docsDir = writable(preferred) ? preferred : writable(shared) ? shared : new File(priv, "Documents");
        return L;
    }

    private boolean inspect(File dir, String hash) {
        searched.add(dir);
        if (consider(dir, hash)) return true;
        File[] children = dir.listFiles();
        if (children != null) {
            Arrays.sort(children);
            for (File child : children) if (child.isDirectory() && !child.getName().startsWith(".") &&
                    !child.getName().equalsIgnoreCase("Documents") && consider(child, hash)) return true;
        }
        return false;
    }

    private boolean consider(File dir, String hash) {
        Compatibility result = check(dir, hash);
        found = result.status == Status.SUPPORTED;
        if (found || compatibility == null || (compatibility.status == Status.MISSING_FILES &&
                result.missing.size() < compatibility.missing.size())) {
            gameDir = dir;
            compatibility = result;
        }
        return found;
    }

    static String norm(String s) { return s.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]", ""); }

    /** Folders in the storage root that look like DS3: deadspace3 first, then "Dead Space 3", etc. */
    static List<File> candidates(File root) {
        List<File> out = new ArrayList<>();
        out.add(new File(root, DEFAULT_NAME));
        File[] k = root.listFiles();
        if (k != null) {
            Arrays.sort(k);
            for (File f : k) if (f.isDirectory() && !f.getName().equals(DEFAULT_NAME) && norm(f.getName()).startsWith(DEFAULT_NAME)) out.add(f);
        }
        return out;
    }

    /** case-insensitive child lookup (the game's file layer is case-insensitive too) */
    public static List<String> missingFiles(File dir) {
        return scan(dir, null);
    }

    private static List<String> scan(File dir, File[] exe) {
        List<String> missing = new ArrayList<>(Arrays.asList("deadspace3.exe", "deadspace3.par",
                "bigfile0.viv", "bigfile1.viv", "bigfile2.viv", "bigfile3.viv", "bigfile4.viv", "bigfile5.viv", "bigfile.patch.viv"));
        File[] files = dir == null ? null : dir.listFiles();
        if (files != null) {
            Arrays.sort(files);
            for (File f : files) {
                String name = f.getName().toLowerCase(Locale.ROOT);
                if (!missing.contains(name) || !f.isFile() || !f.canRead() || f.length() == 0) continue;
                missing.remove(name);
                if (exe != null && name.equals("deadspace3.exe")) exe[0] = f;
            }
        }
        return missing;
    }
    public static boolean isGame(File d) {
        return check(d, EXE_SHA256).status == Status.SUPPORTED;
    }

    /** Launcher preflight, before creating diagnostics; the runtime checks again. */
    public static boolean safeDocuments(File game, File docs) {
        try {
            File g = game.getCanonicalFile(), d = docs.getCanonicalFile();
            if (g.toPath().startsWith(d.toPath()) ||
                    (d.toPath().startsWith(g.toPath()) && !d.equals(new File(g, "Documents")))) return false;
            File drive = new File(d, "drive_c");
            return drive.equals(drive.getCanonicalFile()) && !java.nio.file.Files.isSymbolicLink(drive.toPath()) &&
                    (!d.exists() || d.isDirectory()) && (!drive.exists() || drive.isDirectory());
        } catch (IOException | SecurityException e) {
            return false;
        }
    }

    private static Compatibility check(File dir, String hash) {
        File[] exe = new File[1];
        List<String> missing = scan(dir, exe);
        if (!missing.isEmpty()) return new Compatibility(Status.MISSING_FILES, missing);
        try (FileInputStream in = new FileInputStream(exe[0])) {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) != -1;) digest.update(buf, 0, n);
            byte[] sum = digest.digest();
            for (int i = 0; i < sum.length; i++)
                if ((sum[i] & 255) != Integer.parseInt(hash.substring(i * 2, i * 2 + 2), 16))
                    return new Compatibility(Status.UNSUPPORTED_EXE, missing);
            return new Compatibility(Status.SUPPORTED, missing);
        } catch (IOException e) {
            return new Compatibility(Status.UNREADABLE_EXE, missing);
        } catch (NoSuchAlgorithmException e) {
            throw new AssertionError(e); // SHA-256 is required on Java and Android.
        }
    }
    private static boolean writable(File docs) {
        if (docs == null) return false;
        File dir = docs.exists() ? docs : docs.getParentFile();
        // An absent game's Documents path is only a candidate, not a directory to create during discovery.
        return dir.isDirectory() && dir.canWrite();
    }
}
