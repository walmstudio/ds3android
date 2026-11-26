# Dead Space 3 — Linux и Android из одного дерева

Основа — Linux №75. `runtime/` и `settings/` общие; один `Makefile` компилирует их отдельно для x86‑64 и ARM64. `linux/` — SDL2, оконная система, лаунчер и звуковые выходы. `android/` — лаунчер, сенсорное управление №52, JNI и упаковка APK. Слепков `gen*`, готовых объектников и гостевого кода в этом дереве нет.

```bash
./build-linux.sh
KEYSTORE=/путь/к/локальному/ds3port-debug.keystore ./build-android.sh
```

По умолчанию локальные входы читаются из `../build-inputs`, результаты идут в `../build-output/linux` или `../build-output/android`. Можно задать `DS3_INPUTS=/абсолютный/путь` и передать новый каталог результата первым аргументом. `JOBS` по умолчанию равен `nproc`. Скрипты не устанавливают и не публикуют сборки. При прямом вызове `make` задавайте отдельный `BUILD` для каждого набора входов и инструментов; скрипты делают это автоматически.

- **Linux x86‑64:** GNU make, C-компилятор, `pkg-config`, SDL2 ≥ 2.26, Python 3. Нужны `linux/{base,wide,dispatch}` — аттестованный гостевой C №75, а также `linux-x86_64/{image.bin,libme1spv.so}` и заголовки PipeWire/SPA в `pipewire-headers/`. Все `.o` компилируются заново. После сборки: `../build-output/linux/ds3-launcher`.
- **Android ARM64, Android 10+:** GNU make, NDK r27c, JDK 17, SDK Platform 34, Build Tools 34.0.0, `zip`. Использует **тот же** комплект гостевого C №75 (`linux/{base,wide,dispatch}`) и `linux-x86_64/image.bin`. Дополнительно нужны ARM64 `android-arm64/libme1spv.so` и существующий `KEYSTORE`. `NDK`, `ANDROID_SDK`, `ANDROID_BT`, `ANDROID_JAR` и `JAVA_HOME` можно переопределить. Результат — `../build-output/android/ds3port.apk`.

Название `linux/` у приватных гостевых входов историческое: обе цели компилируют один и тот же набор C-файлов в машинный код своей платформы. Игровые образы, generated C, SPIR-V библиотеки и ключ остаются локальными и не входят в дерево исходников.
