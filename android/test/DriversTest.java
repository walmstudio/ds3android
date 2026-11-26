package com.ds3port.launcher;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Host-only renderer and local-package fixture. The synthetic ELF inputs are never loaded. */
public class DriversTest {
    private static int failures;
    private static final String META = "{\"name\":\"Fixture \\\"ICD\\\"\",\"description\":\"Sample\\nTest\","
            + "\"minApi\":29,\"vendor\":\"Mali\",\"libraryName\":\"libfixture.so\",\"extra\":{\"x\":[1,true]}}";

    private static void check(boolean pass, String message) {
        System.out.println((pass ? "ok   " : "FAIL ") + message);
        if (!pass) failures++;
    }

    private static void zip(File file, Object... pairs) throws IOException {
        try (ZipOutputStream out = new ZipOutputStream(Files.newOutputStream(file.toPath()))) {
            for (int i = 0; i < pairs.length; i += 2) {
                out.putNextEntry(new ZipEntry((String) pairs[i]));
                Object data = pairs[i + 1];
                out.write(data instanceof byte[] ? (byte[]) data : data.toString().getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
    }

    private static GpuDrivers.Driver find(List<GpuDrivers.Driver> entries, String name) {
        return entries.stream().filter(d -> d.src.getName().equals(name)).findFirst().orElseThrow(AssertionError::new);
    }

    private static void rejects(File base, GpuDrivers.Driver driver, String reason) throws Exception {
        try {
            GpuDrivers.install(base, driver);
            check(false, driver.src.getName() + " rejected");
        } catch (IOException e) {
            check(e.getMessage().contains(reason), driver.src.getName() + " rejected: " + e.getMessage());
        }
        check(!base.exists() || base.list().length == 0, "failed selection leaves no staged files");
    }

    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("ds3-drivers-");
        try {
            byte[] icd = Files.readAllBytes(new File(args[0]).toPath());
            byte[] hal = Files.readAllBytes(new File(args[1]).toPath());
            byte[] dep = Files.readAllBytes(new File(args[2]).toPath());
            byte[] noNegotiate = Files.readAllBytes(new File(args[3]).toPath());
            File game = Files.createDirectory(root.resolve("game")).toFile();
            File cache = Files.createDirectory(root.resolve("cache")).toFile();
            File base = root.resolve("staged").toFile();
            File dir = new File(game, "GameExDriver");
            check(GpuDrivers.scan(game, cache).isEmpty() && !dir.exists(), "scanning never creates GameExDriver");
            dir.mkdir();
            File original = new File(dir, "chosen.zip");
            zip(original, "meta.json", META, "libfixture.so", icd, "README.txt", "never extracted");
            zip(new File(dir, "nested.zip"), "bundle/meta.json", META, "bundle/libfixture.so", icd);
            zip(new File(dir, "ambiguous.zip"), "meta.json", META, "bundle/meta.json", META, "libfixture.so", icd);
            zip(new File(dir, "truncated.zip"), "meta.json", META.substring(0, META.length() - 1), "libfixture.so", icd);
            byte[] intact = Files.readAllBytes(original.toPath());
            Files.write(new File(dir, "same-bytes.zip").toPath(), intact);
            Files.write(new File(dir, "incomplete-archive.zip").toPath(), Arrays.copyOf(intact, intact.length - 8));
            zip(new File(dir, "duplicate-key.zip"), "meta.json", META.replace("\"name\"", "\"libraryName\":\"other.so\",\"name\""), "libfixture.so", icd);
            zip(new File(dir, "wrong-member.zip"), "meta.json", META, "libvulkan_guess.so", icd);
            zip(new File(dir, "hal.zip"), "meta.json", META, "libfixture.so", hal);
            zip(new File(dir, "dependent.zip"), "meta.json", META, "libfixture.so", dep);
            zip(new File(dir, "no-negotiate.zip"), "meta.json", META, "libfixture.so", noNegotiate);
            byte[] wrongArch = icd.clone(); wrongArch[18] = 62;
            zip(new File(dir, "arch.zip"), "meta.json", META, "libfixture.so", wrongArch);
            zip(new File(dir, "traversal.zip"), "meta.json", META, "libfixture.so", icd, "../other.so", icd);
            zip(new File(dir, "large.zip"), "meta.json", META + new String(new char[70_000]).replace('\0', ' '), "libfixture.so", icd);
            List<GpuDrivers.Driver> list = GpuDrivers.scan(game, cache);
            GpuDrivers.Driver chosen = find(list, "chosen.zip"), nested = find(list, "nested.zip");
            check(chosen.error == null && "Fixture \"ICD\"".equals(chosen.name) && "Sample\nTest".equals(chosen.description)
                    && "libfixture.so".equals(chosen.libraryName), "root metadata and exact libraryName listed");
            check(nested.error == null && "libfixture.so".equals(nested.libraryName), "one-folder metadata listed");
            check(!base.exists(), "listing leaves private storage untouched");
            check(cache.list().length == 0, "browse snapshots removed after valid and truncated ZIPs");
            java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
            digest.update((original.getCanonicalPath() + "\0").getBytes(StandardCharsets.UTF_8));
            String expectedId = String.format("%064x", new java.math.BigInteger(1, digest.digest(intact)));
            check(expectedId.equals(chosen.id) && !expectedId.equals(find(list, "same-bytes.zip").id),
                    "stable ZIP identity binds the original path and exact archive bytes");
            check(GpuDrivers.compat(chosen, "Adreno 640", 29, "10").contains("Mali"), "GPU family mismatch explained");
            check(GpuDrivers.compat(chosen, "Mali G78", 28, "9").contains("29"), "minimum API mismatch explained");
            for (String name : new String[]{"ambiguous", "duplicate-key", "wrong-member", "traversal", "large"})
                rejects(base, find(list, name + ".zip"), "");
            rejects(base, find(list, "truncated.zip"), "JSON в meta.json");
            rejects(base, find(list, "incomplete-archive.zip"), "Повреждённый или неподдерживаемый ZIP");
            rejects(base, find(list, "hal.zip"), "HAL");
            rejects(base, find(list, "arch.zip"), "ARM64");
            rejects(base, find(list, "dependent.zip"), "libfixturedep.so");
            rejects(base, find(list, "no-negotiate.zip"), "vk_icdNegotiateLoaderICDInterfaceVersion");

            File staged = GpuDrivers.install(base, chosen);
            check(staged.isFile() && Arrays.equals(icd, Files.readAllBytes(staged.toPath())) && base.list().length == 1,
                    "explicit staging publishes only the verified library bytes");
            File imports = root.resolve("imports").toFile();
            String imported = GpuDrivers.importIcd(imports, new java.io.ByteArrayInputStream(icd));
            Map<String, String> env = new HashMap<>();
            env.put("DS3_VK_DRIVER", "old");
            Ds3VideoOptions.putEnv(env, null, null, imports);
            check("gles".equals(env.get("DS3_RENDERER")) && !env.containsKey("DS3_VK_DRIVER"), "GLES remains the default");
            Ds3VideoOptions.putEnv(env, "custom", imported, imports);
            check("vulkan".equals(env.get("DS3_RENDERER")) && new File(imports, imported).getPath().equals(env.get("DS3_VK_DRIVER")),
                    "existing standalone selection keeps its original route");
            Ds3VideoOptions.putEnv(env, "vulkan", imported, imports);
            check("vulkan".equals(env.get("DS3_RENDERER")) && !env.containsKey("DS3_VK_DRIVER"), "system Vulkan has no private driver");

            Ds3VideoOptions.putZipEnv(env, game, base, original.getName(), chosen.id);
            check("vulkan".equals(env.get("DS3_RENDERER")) && staged.getPath().equals(env.get("DS3_VK_DRIVER")),
                    "selected ZIP uses the same private standalone ICD route");
            long oldTime = original.lastModified();
            byte[] savedArchive = Files.readAllBytes(original.toPath());
            byte[] replacement = savedArchive.clone();
            replacement[10] ^= 1; // Local ZIP header timestamp: different bytes, same length and valid ZIP.
            Files.write(original.toPath(), replacement);
            Files.setLastModifiedTime(original.toPath(), java.nio.file.attribute.FileTime.fromMillis(oldTime));
            rejectsLaunch(env, game, base, original, chosen, "replaced ZIP");
            Files.write(original.toPath(), savedArchive);
            Ds3VideoOptions.putZipEnv(env, game, base, original.getName(), chosen.id);
            Files.delete(original.toPath());
            check(find(GpuDrivers.scan(game, cache), "nested.zip").error == null, "another archive remains after selected ZIP removal");
            rejectsLaunch(env, game, base, original, chosen, "missing ZIP");
            try {
                GpuDrivers.install(base, chosen);
                check(false, "deleted selection is never silently reused or replaced");
            } catch (IOException e) {
                check(true, "deleted selection is never silently reused or replaced");
            }
        } finally {
            try (java.util.stream.Stream<Path> entries = Files.walk(root)) {
                for (Path path : (Iterable<Path>) entries.sorted(Comparator.reverseOrder())::iterator) Files.delete(path);
            }
        }
        if (failures != 0) throw new AssertionError(failures + " renderer cases failed");
    }

    private static void rejectsLaunch(Map<String, String> env, File game, File base,
                                      File original, GpuDrivers.Driver chosen, String problem) throws Exception {
        try {
            Ds3VideoOptions.putZipEnv(env, game, base, original.getName(), chosen.id);
            check(false, problem + " blocks launch, even with a staged copy and another driver");
        } catch (IOException e) {
            check(!env.containsKey("DS3_VK_DRIVER"), problem + " blocks launch and clears the previous driver path");
        }
    }
}
