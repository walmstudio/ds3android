package com.ds3port.launcher;
import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;

/** Host-only folder check; never opens or modifies an installed game. */
public class LocatorTest {
    static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
        System.out.println("ok  " + name);
    }

    static void touch(File root, String name) throws Exception {
        File file = new File(root, name);
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), new byte[] { 1 });
    }

    static void stock(File root) throws Exception {
        touch(root, "DeadSpace3.EXE");
        touch(root, "deadspace3.par");
        for (int i = 0; i <= 5; i++) touch(root, "Bigfile" + i + ".VIV");
    }

    static GameLocator locate(java.util.List<File> roots, File priv, String path) {
        return GameLocator.locate(roots, priv, path,
                "4bf5122f344554c53bde2ebb8cd2b7e3d1600ad631c385a5d7cce23c7785459a");
    }

    static void rm(File file) {
        File[] files = file.listFiles();
        if (files != null) for (File child : files) rm(child);
        file.delete();
    }

    public static void main(String[] args) throws Exception {
        File tmp = Files.createTempDirectory("ds3-locator").toFile();
        try {
            File sd = new File(tmp, "sdcard"), card = new File(tmp, "card"), priv = new File(tmp, "app");
            File base = new File(sd, "deadspace3"), wrapper = new File(card, "Dead Space 3 Backup");
            File nested = new File(wrapper, "Game"), privateGame = new File(priv, "deadspace3");
            java.util.List<File> roots = Arrays.asList(sd, card);
            touch(base, "Binaries/deadspace3.exe");
            touch(base, "BioGame/Config/DefaultEngine.ini");
            touch(base, "Binaries/MassEffect.exe");
            touch(base, "BioGame/CookedPC/Package.upk");
            check(!locate(roots, priv, null).found, "old ME1 Binaries/BioGame layout rejected");
            stock(nested);
            check(GameLocator.missingFiles(nested).contains("bigfile.patch.viv"), "missing patch archive is diagnosed");
            touch(nested, "BigFile.PATCH.VIV");
            check(!GameLocator.isGame(nested), "complete fixture with wrong executable hash is rejected");
            check(GameLocator.locate(roots, priv, wrapper.getPath()).compatibility.status == GameLocator.Status.UNSUPPORTED_EXE,
                    "manual wrapper reports unsupported EXE rather than missing files");
            GameLocator loc = locate(roots, priv, null);
            check(loc.found && loc.gameDir.equals(nested), "case-insensitive stock root on SD card, one level down");
            stock(base);
            touch(base, "bigfile.patch.viv");
            loc = locate(roots, priv, null);
            check(loc.found && loc.gameDir.equals(base) && loc.docsDir.equals(new File(base, "Documents")), "default location wins; same diagnostics directory");
            File documents = new File(base, "Documents");
            Files.createSymbolicLink(documents.toPath(), base.toPath());
            try {
                check(!GameLocator.safeDocuments(base, documents), "launcher refuses an overlapping Documents mount before writes");
            } finally { Files.delete(documents.toPath()); }
            check(GameLocator.safeDocuments(base, documents), "launcher accepts the genuine Documents mount");
            loc = locate(roots, priv, wrapper.getPath());
            check(loc.manual && loc.found && loc.gameDir.equals(nested), "manual wrapper overrides auto result");
            Files.delete(new File(nested, "deadspace3.par").toPath());
            check(!locate(roots, priv, nested.getPath()).found, "missing PAR rejects explicit path without fallback");
            touch(nested, "deadspace3.par");
            File archive = new File(nested, "Bigfile5.VIV");
            Files.delete(archive.toPath());
            Files.createDirectory(archive.toPath());
            check(!GameLocator.isGame(nested), "directory cannot stand in for a base archive");
            File absent = new File(tmp, "absent");
            check(!GameLocator.locate(roots, priv, absent.getPath()).found && !absent.exists(), "checking absent manual path does not create it");
            stock(privateGame);
            touch(privateGame, "bigfile.patch.viv");
            touch(priv, "Documents/BioWare/Dead Space 3/Config/ds3port.ini");
            Files.delete(new File(base, "DeadSpace3.EXE").toPath());
            loc = locate(roots, priv, null);
            check(loc.found && loc.legacyPrivate && loc.gameDir.equals(privateGame), "private fallback without Binaries duplicate");
            check(!new File(loc.docsDir, "BioWare").exists() && new File(priv, "Documents/BioWare/Dead Space 3/Config/ds3port.ini").isFile(), "no migration of existing files");
        } finally {
            rm(tmp);
        }
    }
}
