# Пропатченные файлы порта Dead Space 3

Копия всех файлов, которые я изменял в этом разговоре. Структура каталогов повторяет
`sources/`, поэтому файлы можно накладывать на дерево исходников один в один:

```bash
cp -a /home/webmast/ds3an/patched-files/. /home/webmast/ds3an/sources/
```

Скопированные файлы побайтово совпадают с текущими в `sources/` (проверено `sha256sum`).
Ниже — что именно изменено в каждом. Номера строк — по состоянию на 27.11.2026.

## Настройки порта

| файл | изменение |
|---|---|
| `settings/ds3settings.c` | добавлена `ds3cfg_apply_env()` (строка 129): читает `ds3port.ini` и выставляет `ME1_*` через `setenv(..., 0)`, то есть явная переменная окружения имеет приоритет. Процент нагрева переводится в долю. Схема настроек урезана до 12 поддерживаемых пунктов + 4 пресета; `port.simple_shaders`, `port.touch_controls`, `port.touch_opacity`, `port.phone_mod` отклоняются как неизвестные. |
| `settings/ds3settings.h` | объявление `ds3cfg_apply_env()` (строка 22). |

## Рантайм

| файл | изменение |
|---|---|
| `runtime/main_ds3.c` | `default_image()` (строка 22): `DS3_IMAGE` → `image.bin` рядом с исполняемым файлом → `out/exe/image.bin`. `default_docs()` (строка 41): `$XDG_DATA_HOME/ds3port/Documents` или `~/.local/share/ds3port/Documents`. Вызов `ds3cfg_apply_env(docs)` (строка 87) до `hle_fs_init`. |
| `runtime/main_ds3_module.c` | self-check берёт образ из `DS3_IMAGE`, по умолчанию `out/exe/image.bin` (строка 88). |
| `runtime/hle_al.c` | AAudio откачен к исходному: `PERFORMANCE_MODE_NONE` (10) на строке 590 и буфер `burst*4` с полом `rate/25` (≥ 40 мс) на строке 606. |
| `runtime/check_ds3_funcs.py` | добавлен `--seeds-out=FILE` (строка 123): пишет адреса кандидатов для `recomp.py --seeds`. |
| `runtime/ds3_extra_seeds.txt` | **новый файл**: 9 адресов функций, которые рекомпилятор не нашёл. |

## Linux

| файл | изменение |
|---|---|
| `linux/launcher.c` | `export_port_env()` удалён — лаунчер больше не дописывает ни одной переменной. `docs_is_default()` (строка 1000): `--docs` передаётся только для нестандартного профиля (строка 1015). Генератор PhoneModify, его карточка, пункт в списке переменных и CLI-хук `modgen` удалены. |
| `linux/ds3_wayland.c` | разбор аргументов (строка 204) принимает обе формы: `--game DIR [--docs DIR] [--lang ru]` и позиционную `GAME [DOCS]`; `--language` и `--lang` равнозначны, `DOCS` необязателен. |

## Сборка и документация

| файл | изменение |
|---|---|
| `Makefile` | `settings/ds3settings.c` добавлен в `COMMON` (строка 10), иначе рантайм не слинкуется. |
| `build-linux.sh` | каталог результата можно указывать повторно: прошлая сборка перезаписывается на месте, лишние артефакты удаляются по старому `SHA256SUMS`. Отклоняются `/`, `$HOME`, корень `sources` и его подкаталоги, непустой посторонний каталог, симлинк. Больше одного аргумента → `usage`. |
| `build-android.sh` | та же логика каталога результата, признак прошлой сборки — `SHA256SUMS` + `ds3port.apk`. |
| `README.md` | раздел «Настройки порта», необязательные `--docs` и `DS3_IMAGE`, новое поведение каталога результата. |

## Что не входит

- Гостевой C (`linux/base`, `linux/wide`, `linux/dispatch`) не перегенерирован: девять функций из
  `runtime/ds3_extra_seeds.txt` попадут в сборку только после `recomp.py --seeds`.
- `linux/{base,wide,dispatch}`, образы, `libme1spv.so` и ключ подписи остаются локальными.
- Android-файлы (`MainActivity.java`, `ds3port_jni.c`, `DeviceInfo.java`) в этот набор не включены:
  в этом разговоре я их не правил, они лишь читают ту же схему через JNI.

## Проверки

- `sha256sum -c SHA256SUMS` в этом каталоге — все файлы «ЦЕЛ».
- `build-linux.sh` → exit 0, 7m11s, `build-output/linux/SHA256SUMS` сходится по всем 8 файлам.
- `test-desktop-route.py`, `test-render-settings.py` — OK (внутри скрипта сборки).
- `run_input_tests.sh` — все PASS (прогон до последней правки скриптов).
- APK после отката `hle_al.c` собран: `build-output/android-0409/ds3port.apk`.
