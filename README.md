# Synapse iPA

iOS-дилб меню для Unity (il2cpp) игр. Сборка — arm64, минимальная версия iOS 14.0.

## Структура

```
Src/
  main.mm              — входная точка, оверлей-окно + меню ImGui (Metal)
  Features/            — вкладки меню (Visuals, Aim, Rage, Misc)
  Hooks/               — хуки через Dobby (hooks.mm — сюда добавлять свои)
  il2cpp/              — обёртка над il2cpp (dlsym UnityFramework)
  Memory/              — KittyMemory + сканеры, Keystone в Deps
  ImGui/               — Dear ImGui + Metal бэкенд
  Dobby/               — submodule github.com/jmpews/Dobby (инлайн-хукинг)
.github/workflows/     — CI: сборка на macos-15
build.sh              — локальная сборка на macOS (то же, что в CI)
```

## Сборка

**CI (основной способ):** push в `main` — GitHub Actions сам соберёт `Synapse.dylib` и выложит артефакт.
Submodule подтягивается автоматически (`submodules: recursive`).

**Локально (нужен Mac с Xcode):**

```sh
git submodule update --init --recursive
./build.sh
```

## Как добавить хук (Dobby)

Хуки живут в `Src/Hooks/hooks.mm`. Dobby уже собран и слинкован (`dobby-build/libdobby.a`), заголовок — `#include <dobby.h>`.

1. Объяви тип функции и оригинал:

```cpp
using Player_Update_t = void (*)(void *player);
static Player_Update_t orig_Player_Update;

static void hook_Player_Update(void *player) {
    orig_Player_Update(player);   // вызов оригинала
    // своя логика
}
```

2. Установи хук в `installGameHooks()` (вызывается автоматически, когда il2cpp готов):

```cpp
void *klass  = IL2CPP::Class("", "PlayerController");
void *method = IL2CPP::Method(klass, "Update", 0);
if (method)
    DobbyHook(method, (void *)hook_Player_Update, (void **)&orig_Player_Update);
```

`DobbyHook` возвращает `0` при успехе. Также доступны `DobbyDestroy` (снять хук),
`DobbySymbolResolver` (резолв символа по имени образа), `DobbyCodePatch` (патч кода).

При загрузке дилб делает self-test Dobby (хукает собственную функцию) —
в логе должно появиться `[Hooks] Dobby <version> self-test OK`.

## Обновление Dobby

```sh
git submodule update --remote Src/Dobby
git add Src/Dobby
```
