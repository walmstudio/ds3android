package com.ds3port.launcher;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Local metadata discovery and explicitly trusted, single-file staging for the standalone ICD route. */
final class GpuDrivers {
    static final String FOLDER = "GameExDriver";
    private static final long FILE_LIMIT = 128L << 20;
    private static final int META_LIMIT = 64 << 10, ZIP_ENTRIES = 256, ZIP_INDEX_LIMIT = 256 << 10;
    private static final LinkOption NO_LINK = LinkOption.NOFOLLOW_LINKS;
    // Deliberately conservative: no bundled sibling libraries, private vendor libs or linker redirection.
    private static final Set<String> ANDROID_LIBS = new HashSet<>(Arrays.asList("libc.so", "libdl.so", "libm.so",
            "liblog.so", "libandroid.so", "libz.so", "libvulkan.so", "libnativewindow.so", "libsync.so"));

    static final class Driver {
        final File src;
        String id, error, member;
        byte[] metadata;
        String name, description = "", author = "", packageVersion = "", vendor = "", driverVersion = "", libraryName = "";
        int minApi;

        Driver(File src) { this.src = src; name = src.getName(); }

        String label() {
            return name + (driverVersion.isEmpty() || name.contains(driverVersion) ? "" : " " + driverVersion)
                    + (vendor.isEmpty() ? "" : " (" + vendor + ")");
        }

        String details() {
            return src.getName() + (libraryName.isEmpty() ? "" : " · " + libraryName)
                    + (packageVersion.isEmpty() ? "" : " · " + packageVersion)
                    + (author.isEmpty() ? "" : " · " + author)
                    + (minApi == 0 ? "" : " · Android API ≥ " + minApi)
                    + (description.isEmpty() ? "" : "\n" + description);
        }

        String family() { return GpuDrivers.family(vendor + " " + name + " " + description + " " + libraryName); }
    }

    static String family(String text) {
        String t = text.toLowerCase(Locale.ROOT);
        if (t.matches("(?s).*(adreno|qualcomm|turnip|freedreno|snapdragon|vulkan\\.adr|qcom).*")) return "adreno";
        if (t.matches("(?s).*(mali|panfrost|panvk).*")) return "mali";
        if (t.matches("(?s).*(powervr|imagination|pvr).*")) return "powervr";
        if (t.matches("(?s).*(xclipse|radv|samsung).*")) return "xclipse";
        return "";
    }

    static String familyName(String f) {
        return f.equals("adreno") ? "Adreno (Qualcomm)" : f.equals("mali") ? "Mali (ARM)" : f.equals("powervr") ? "PowerVR" : f.equals("xclipse") ? "Xclipse (Samsung)" : "?";
    }

    static String compat(Driver d, String gpu, int sdk, String release) {
        if (d.minApi > sdk) return "Требуется Android API " + d.minApi + "; у вас API " + sdk + " (Android " + release + ").";
        String expected = d.family(), actual = family(gpu);
        if (!expected.isEmpty() && !actual.isEmpty() && !expected.equals(actual))
            return "По описанию пакет для " + familyName(expected) + "; устройство: " + familyName(actual) + ". Это предупреждение, а не проверка совместимости.";
        return "";
    }

    static File folder(File gameDir) { return new File(gameDir, FOLDER); }

    /** Metadata only. At most 512 directory records, 32 ZIPs and 512 MiB of source reads per refresh. */
    static List<Driver> scan(File gameDir, File privateCache) {
        List<Driver> result = new ArrayList<>();
        File dir = folder(gameDir);
        if (!Files.isDirectory(dir.toPath(), NO_LINK)) return result;
        int visited = 0;
        long budget = 512L << 20;
        try (DirectoryStream<Path> files = Files.newDirectoryStream(dir.toPath())) {
            for (Path file : files) {
                if (++visited > 512 || result.size() >= 32 || Thread.currentThread().isInterrupted()) break;
                String name = file.getFileName().toString();
                if (!isZipName(name) || !Files.isRegularFile(file, NO_LINK)) continue;
                Driver d = new Driver(file.toFile());
                result.add(d);
                Path snapshot = null;
                try {
                    File source = source(gameDir, name);
                    // Use the shared file's size only to reserve budget, including a one-byte EOF check.
                    long size = Files.size(source.toPath());
                    if (size > FILE_LIMIT) throw new IOException("Архив больше 128 МиБ.");
                    if (size >= budget) throw new IOException("Лимит чтения списка исчерпан.");
                    budget -= size + 1;
                    snapshot = Files.createTempFile(privateCache.toPath(), "ds3-browse-", ".part");
                    String id = archiveId(source, snapshot, size);
                    try (ZipFile zip = openZip(snapshot.toFile())) { readPackage(zip, d); }
                    d.id = id;
                } catch (IOException | IllegalArgumentException e) {
                    d.error = e.getMessage() == null ? "Не удалось прочитать ZIP." : e.getMessage();
                } finally {
                    if (snapshot != null) Files.deleteIfExists(snapshot);
                }
            }
        } catch (IOException | java.nio.file.DirectoryIteratorException ignored) { }
        result.sort(Comparator.comparing(d -> d.src.getName(), String.CASE_INSENSITIVE_ORDER));
        return result;
    }

    private static boolean isZipName(String name) {
        return name != null && !name.startsWith(".") && name.indexOf('/') < 0 && name.indexOf('\\') < 0
                && name.toLowerCase(Locale.ROOT).endsWith(".zip");
    }

    private static File source(File gameDir, String name) throws IOException {
        File dir = folder(gameDir.getCanonicalFile());
        if (!isZipName(name) || !Files.isDirectory(dir.toPath(), NO_LINK)) throw missing();
        File source = new File(dir, name);
        if (!source.equals(source.getCanonicalFile()) || !Files.isRegularFile(source.toPath(), NO_LINK)) throw missing();
        return source;
    }

    private static IOException missing() {
        return new IOException("Выбранный ZIP отсутствует или изменён. Выберите пакет заново либо переключитесь на системный рендерер.");
    }

    /** Bind an archive to its exact path and bytes, not its label, CRC, size or mtime. */
    private static String archiveId(File source, Path snapshot, long limit) throws IOException {
        MessageDigest digest;
        try { digest = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new AssertionError(e); }
        digest.update(source.getPath().getBytes(StandardCharsets.UTF_8));
        digest.update((byte) 0);
        try (InputStream in = Files.newInputStream(source.toPath(), NO_LINK);
             OutputStream out = snapshot == null ? null : Files.newOutputStream(snapshot)) {
            byte[] buf = new byte[65536];
            long total = 0;
            int n;
            while (true) {
                if (Thread.currentThread().isInterrupted()) throw missing();
                n = in.read(buf, 0, (int) Math.min(buf.length, limit - total + 1));
                if (n == -1) break;
                if ((total += n) > limit) throw missing();
                digest.update(buf, 0, n);
                if (out != null) out.write(buf, 0, n);
            }
        }
        StringBuilder hex = new StringBuilder(64);
        for (byte b : digest.digest()) hex.append(Character.forDigit((b & 255) >>> 4, 16)).append(Character.forDigit(b & 15, 16));
        return hex.toString();
    }

    /** Bound the central directory before letting ZipFile index it. Only single-disk, non-ZIP64 archives. */
    private static ZipFile openZip(File file) throws IOException {
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long size = in.length();
            if (size > FILE_LIMIT) throw new IOException("Архив больше 128 МиБ.");
            int tail = (int) Math.min(size, 65535 + 22);
            ByteBuffer b = read(in, size - tail, tail);
            for (int i = tail - 22; i >= 0; i--) {
                if (b.getInt(i) != 0x06054b50 || u16(b, i + 20) != tail - i - 22) continue;
                int entries = u16(b, i + 10);
                long index = Integer.toUnsignedLong(b.getInt(i + 12));
                long off = Integer.toUnsignedLong(b.getInt(i + 16));
                if (b.getInt(i + 4) == 0 && u16(b, i + 8) == entries && entries <= ZIP_ENTRIES
                        && index <= ZIP_INDEX_LIMIT && off + index == size - tail + i)
                    return new ZipFile(file);
                break;
            }
            throw new IOException("Повреждённый или неподдерживаемый ZIP (размер каталога, ZIP64 или несколько томов).");
        }
    }

    private static void readPackage(ZipFile zip, Driver driver) throws IOException {
        if (zip.size() > ZIP_ENTRIES) throw new IOException("Слишком много записей в ZIP.");
        Set<String> names = new HashSet<>();
        ZipEntry meta = null;
        for (Enumeration<? extends ZipEntry> it = zip.entries(); it.hasMoreElements();) {
            ZipEntry entry = it.nextElement();
            String path = entry.getName();
            String clean = entry.isDirectory() ? path.substring(0, path.length() - 1) : path;
            String[] parts = clean.split("/", -1);
            if (path.length() > 512 || path.indexOf('\\') >= 0 || path.indexOf(':') >= 0
                    || !names.add(clean.toLowerCase(Locale.ROOT))) throw new IOException("Неоднозначный или недопустимый путь в ZIP.");
            for (String part : parts) {
                if (part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Недопустимый путь в ZIP.");
            }
            if (parts[parts.length - 1].equalsIgnoreCase("meta.json")) {
                if (meta != null || entry.isDirectory() || parts.length > 2 || !parts[parts.length - 1].equals("meta.json"))
                    throw new IOException("Ожидается один meta.json в корне ZIP или в одной папке.");
                meta = entry;
            }
        }
        if (meta == null) throw new IOException("В ZIP нет meta.json.");
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        copyEntry(zip, meta, buffer, META_LIMIT);
        driver.metadata = buffer.toByteArray();
        String json = StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(driver.metadata)).toString();
        Map<String, String> fields = new MetaJson(json).parse();
        driver.name = fields.getOrDefault("name", driver.name);
        if (driver.name.isEmpty()) driver.name = driver.src.getName();
        driver.description = fields.getOrDefault("description", "");
        driver.vendor = fields.getOrDefault("vendor", "");
        driver.author = fields.getOrDefault("author", "");
        driver.packageVersion = fields.getOrDefault("packageVersion", "");
        driver.driverVersion = fields.getOrDefault("driverVersion", "");
        String api = fields.getOrDefault("minApi", "0");
        if (!api.matches("[0-9]{1,4}")) throw new IOException("Недопустимое значение minApi в meta.json.");
        driver.minApi = Integer.parseInt(api);
        String library = fields.get("libraryName");
        if (library == null || !library.matches("[A-Za-z0-9_][A-Za-z0-9_.+-]{0,200}\\.so"))
            throw new IOException("В meta.json требуется точное имя файла .so (libraryName), без пути.");
        driver.libraryName = library;
        driver.member = meta.getName().substring(0, meta.getName().length() - "meta.json".length()) + library;
        ZipEntry entry = zip.getEntry(driver.member);
        if (entry == null || entry.isDirectory() || !entry.getName().equals(driver.member) || entry.getSize() <= 0)
            throw new IOException("Указанный в meta.json файл не найден рядом с ним: " + library);
        if (entry.getSize() > FILE_LIMIT) throw new IOException("Библиотека больше 128 МиБ.");
    }

    private static void copyEntry(ZipFile zip, ZipEntry entry, OutputStream out, long limit) throws IOException {
        long size = entry.getSize();
        if (size < 0 || size > limit) throw new IOException("Запись ZIP превышает разрешённый размер: " + entry.getName());
        CRC32 crc = new CRC32();
        long total = 0;
        try (InputStream in = zip.getInputStream(entry)) {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) != -1) {
                if ((total += n) > size || Thread.currentThread().isInterrupted()) throw new IOException("Неверный размер записи ZIP.");
                crc.update(buf, 0, n);
                out.write(buf, 0, n);
            }
        }
        if (total != size || crc.getValue() != entry.getCrc()) throw new IOException("Повреждённая запись ZIP: " + entry.getName());
    }

    /** Called only after trust confirmation; validate the exact private snapshot that was selected. */
    static File install(File base, Driver driver) throws IOException {
        if (driver.error != null) throw new IOException(driver.error);
        File source = source(driver.src.getParentFile().getParentFile(), driver.src.getName());
        Files.createDirectories(base.toPath());
        Path snapshot = Files.createTempFile(base.toPath(), "zip-", ".part");
        Path library = null;
        try {
            if (!archiveId(source, snapshot, FILE_LIMIT).equals(driver.id)) throw missing();
            library = Files.createTempFile(base.toPath(), "zip-", ".part");
            try (ZipFile zip = openZip(snapshot.toFile())) {
                Driver fresh = new Driver(source);
                readPackage(zip, fresh);
                if (!Arrays.equals(driver.metadata, fresh.metadata) || !driver.member.equals(fresh.member)) throw missing();
                try (OutputStream out = Files.newOutputStream(library)) {
                    copyEntry(zip, zip.getEntry(fresh.member), out, FILE_LIMIT);
                }
            }
            verifyStandalone(library.toFile());
            if (!library.toFile().setReadOnly()) throw new IOException("Не удалось защитить локальную копию ICD от записи.");
            File destination = privateZip(base, driver.id);
            Files.move(library, destination.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return destination;
        } finally {
            if (library != null) Files.deleteIfExists(library);
            Files.deleteIfExists(snapshot);
        }
    }

    static File selectedZip(File gameDir, File base, String name, String id) throws IOException {
        File library = privateZip(base, id);
        if (!Files.isRegularFile(library.toPath(), NO_LINK)) throw missing();
        if (!archiveId(source(gameDir, name), null, FILE_LIMIT).equals(id)) throw missing();
        return library;
    }

    private static File privateZip(File base, String id) throws IOException {
        if (id == null || !id.matches("[0-9a-f]{64}")) throw missing();
        return new File(base.getCanonicalFile(), "zip-" + id + ".so");
    }

    /** Bound retained staging to the current selection; this never walks or touches game storage. */
    static void prune(File base, String keep) throws IOException {
        if (!Files.isDirectory(base.toPath(), NO_LINK)) return;
        try (DirectoryStream<Path> files = Files.newDirectoryStream(base.toPath(), "zip-*")) {
            for (Path p : files) {
                String name = p.getFileName().toString();
                if (!name.equals("zip-" + keep + ".so") && !Files.isDirectory(p, NO_LINK)) Files.deleteIfExists(p);
            }
        }
    }

    /** Explicit legacy local document import. Its IDs and existing selection remain unchanged. */
    static String importIcd(File dir, InputStream in) throws IOException {
        if (!dir.mkdirs() && !dir.isDirectory()) throw new IOException("Cannot create private ICD directory");
        String id = "icd-" + UUID.randomUUID() + ".so";
        File temp = File.createTempFile("import-", ".part", dir);
        try {
            try (OutputStream out = Files.newOutputStream(temp.toPath())) {
                byte[] buf = new byte[65536]; long total = 0; int n;
                while ((n = in.read(buf)) != -1) {
                    if ((total += n) > FILE_LIMIT) throw new IOException("ICD exceeds 128 MiB");
                    out.write(buf, 0, n);
                }
            }
            verifyStandalone(temp);
            if (!temp.setReadOnly()) throw new IOException("Cannot make private ICD read-only");
            Files.move(temp.toPath(), new File(dir, id).toPath(), StandardCopyOption.ATOMIC_MOVE);
            return id;
        } finally {
            Files.deleteIfExists(temp.toPath());
        }
    }

    private static ByteBuffer read(RandomAccessFile in, long off, int len) throws IOException {
        if (off < 0 || len < 0 || off > in.length() - len) throw new IOException("Truncated file or invalid offset");
        byte[] bytes = new byte[len];
        in.seek(off);
        in.readFully(bytes);
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    private static int u16(ByteBuffer b, int at) { return Short.toUnsignedInt(b.getShort(at)); }

    /** Static preflight, NOT proof of GPU support, successful dlopen or ICD interface negotiation. */
    private static void verifyStandalone(File file) throws IOException {
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            if (in.length() < 64) throw new IOException("Expected an Android ARM64 ELF shared library");
            ByteBuffer h = read(in, 0, 64);
            int phCount = u16(h, 56), shCount = u16(h, 60);
            if (h.getInt(0) != 0x464c457f || h.get(4) != 2 || h.get(5) != 1 || h.get(6) != 1
                    || u16(h, 16) != 3 || u16(h, 18) != 183 || h.getInt(20) != 1 || u16(h, 52) != 64
                    || u16(h, 54) != 56 || phCount == 0 || phCount > 128 || h.getLong(32) < 64)
                throw new IOException("Expected an Android ARM64 ELF shared library");
            ByteBuffer headers = read(in, h.getLong(32), 56 * phCount);
            ByteBuffer dynamic = null;
            List<long[]> loads = new ArrayList<>();
            for (int p = 0; p < headers.capacity(); p += 56) {
                int type = headers.getInt(p);
                if (type != 1 && type != 2) continue;
                long off = headers.getLong(p + 8), addr = headers.getLong(p + 16), size = headers.getLong(p + 32);
                if (off < 0 || addr < 0 || size < 0 || size > headers.getLong(p + 40) || off > in.length() - size)
                    throw new IOException("Invalid ELF segment");
                if (type == 1) loads.add(new long[]{off, addr, size});
                else {
                    if (dynamic != null || size < 16 || size > 64 << 10 || size % 16 != 0) throw new IOException("Invalid ELF dynamic table");
                    dynamic = read(in, off, (int) size);
                }
            }
            if (loads.isEmpty() || dynamic == null) throw new IOException("No loadable ELF dynamic table");

            long strAddr = -1, strSize = 0, symAddr = -1, symEntSize = 0;
            List<Long> needed = new ArrayList<>();
            boolean end = false;
            for (int p = 0; p < dynamic.capacity(); p += 16) {
                long type = dynamic.getLong(p), value = dynamic.getLong(p + 8);
                if (type == 0) { end = true; break; }
                if (type == 1) needed.add(value); // DT_NEEDED
                if (type == 5) strAddr = value; // DT_STRTAB
                if (type == 6) symAddr = value; // DT_SYMTAB
                if (type == 10) strSize = value; // DT_STRSZ
                if (type == 11) symEntSize = value; // DT_SYMENT
            }
            if (!end || strSize <= 0 || strSize > 4 << 20 || symEntSize != 24)
                throw new IOException("Cannot verify standalone ICD: invalid ELF dynamic tables");
            ByteBuffer strings = read(in, mappedOffset(loads, strAddr, strSize), (int) strSize);
            if (u16(h, 58) != 64 || shCount == 0 || shCount > 4096 || h.getLong(40) < 64)
                throw new IOException("Cannot verify standalone ICD: missing bounded ELF section table");
            ByteBuffer sections = read(in, h.getLong(40), 64 * shCount);
            ByteBuffer symbols = null;
            for (int p = 0; p < sections.capacity(); p += 64) {
                if (sections.getInt(p + 4) != 11 || sections.getLong(p + 16) != symAddr) continue; // SHT_DYNSYM
                long size = sections.getLong(p + 32), off = sections.getLong(p + 24);
                if (symbols != null || size < 24 || size > 4 << 20 || size % 24 != 0 || sections.getLong(p + 56) != 24
                        || off != mappedOffset(loads, symAddr, size)) throw new IOException("Invalid ELF dynamic symbol table");
                symbols = read(in, off, (int) size);
            }
            if (symbols == null) throw new IOException("Cannot verify standalone ICD: missing ELF dynamic symbols");
            String getProc = "vk_icdGetInstanceProcAddr", negotiate = "vk_icdNegotiateLoaderICDInterfaceVersion";
            boolean hasGetProc = false, hasNegotiate = false, hal = false;
            for (int p = 0; p < symbols.capacity(); p += 24) {
                int info = symbols.get(p + 4) & 255, visibility = symbols.get(p + 5) & 3;
                if (u16(symbols, p + 6) == 0 || (info >>> 4 != 1 && info >>> 4 != 2) || visibility == 1 || visibility == 2) continue;
                long name = Integer.toUnsignedLong(symbols.getInt(p));
                if ((info & 15) == 2) {
                    hasGetProc |= symbolIs(strings, name, getProc);
                    hasNegotiate |= symbolIs(strings, name, negotiate);
                }
                hal |= symbolIs(strings, name, "HMI");
            }
            if (!hasGetProc || !hasNegotiate)
                throw new IOException(hal && !hasGetProc && !hasNegotiate ? "Android HAL driver (HMI); this launcher supports standalone Vulkan ICDs only."
                        : "Not a standalone Vulkan ICD: missing exported " + (!hasGetProc ? getProc : negotiate) + ".");
            for (long ref : needed) {
                String dep = elfString(strings, ref);
                if (!ANDROID_LIBS.contains(dep))
                    throw new IOException("Unsupported ICD dependency: " + dep + ". Bundled or private libraries are not loaded.");
            }
        }
    }

    private static long mappedOffset(List<long[]> loads, long addr, long size) throws IOException {
        for (long[] p : loads)
            if (addr >= p[1] && addr - p[1] <= p[2] - size) return p[0] + addr - p[1];
        throw new IOException("ELF dynamic table is outside loadable segments");
    }

    private static boolean symbolIs(ByteBuffer strings, long off, String name) {
        if (off < 0 || off >= strings.capacity() - name.length()) return false;
        for (int i = 0; i < name.length(); i++) if (strings.get((int) off + i) != name.charAt(i)) return false;
        return strings.get((int) off + name.length()) == 0;
    }

    private static String elfString(ByteBuffer strings, long off) throws IOException {
        if (off < 0 || off >= strings.capacity()) throw new IOException("Invalid ELF dynamic string offset");
        StringBuilder result = new StringBuilder();
        for (int i = (int) off; i < strings.capacity() && result.length() < 256; i++) {
            int c = strings.get(i) & 255;
            if (c == 0) return result.toString();
            if (c < 32 || c > 126) break;
            result.append((char) c);
        }
        throw new IOException("Invalid ELF dependency name");
    }

    /** Small strict JSON reader: unknown values are parsed and ignored, duplicate keys are errors. */
    private static final class MetaJson {
        private static final Pattern TOKEN = Pattern.compile("\"(?:[^\"\\\\\\x00-\\x1f]|\\\\(?:[\"\\\\/bfnrt]|u[0-9a-fA-F]{4}))*+\""
                + "|-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?|true|false|null|[{}\\[\\],:]");
        private final String input;
        private final Matcher matcher;
        private int at;
        private String token;

        MetaJson(String input) { this.input = input; matcher = TOKEN.matcher(input); at = input.startsWith("\uFEFF") ? 1 : 0; }

        Map<String, String> parse() throws IOException {
            next();
            expect("{");
            Map<String, String> result = new HashMap<>();
            object(0, result);
            if (token != null) throw invalid();
            return result;
        }

        private void next() throws IOException {
            while (at < input.length() && " \r\n\t".indexOf(input.charAt(at)) >= 0) at++;
            if (at == input.length()) { token = null; return; }
            if (!matcher.region(at, input.length()).lookingAt()) throw invalid();
            token = matcher.group(); at = matcher.end();
        }

        private boolean take(String expected) throws IOException {
            if (!expected.equals(token)) return false;
            next(); return true;
        }

        private void expect(String expected) throws IOException { if (!take(expected)) throw invalid(); }

        private void object(int depth, Map<String, String> values) throws IOException {
            Set<String> keys = new HashSet<>();
            if (take("}")) return;
            do {
                if (token == null || token.charAt(0) != '"') throw invalid();
                String key = text(token);
                if (!keys.add(key)) throw new IOException("Повтор ключа в meta.json: " + key);
                next(); expect(":");
                boolean scalar = token != null && (token.charAt(0) == '"' || token.matches("-?[0-9].*"));
                String value = value(depth + 1);
                if (values != null && scalar) values.put(key, value);
                if (values != null && key.equals("minApi") && !scalar) throw invalid();
            } while (take(","));
            expect("}");
        }

        private String value(int depth) throws IOException {
            if (depth > 16 || token == null) throw invalid();
            if (take("{")) { object(depth, null); return ""; }
            if (take("[")) {
                if (!take("]")) { do { value(depth + 1); } while (take(",")); expect("]"); }
                return "";
            }
            if ("}],:".indexOf(token.charAt(0)) >= 0) throw invalid();
            String value = token.charAt(0) == '"' ? text(token) : token;
            next(); return value;
        }

        private static String text(String s) {
            StringBuilder out = new StringBuilder();
            for (int i = 1; i < s.length() - 1; i++) {
                char c = s.charAt(i);
                if (c == '\\') {
                    c = s.charAt(++i);
                    int at = "bfnrt".indexOf(c);
                    if (at >= 0) c = "\b\f\n\r\t".charAt(at);
                    else if (c == 'u') { c = (char) Integer.parseInt(s.substring(i + 1, i + 5), 16); i += 4; }
                }
                out.append(c);
            }
            return out.toString();
        }

        private static IOException invalid() { return new IOException("Повреждённый или неподдерживаемый JSON в meta.json."); }
    }
}
