#include "hooks.h"

#include "il2cpp/il2cpp.h"

#include <dobby.h>

#import <Foundation/Foundation.h>

namespace hooks {

// ============================================================================
//  self-test Dobby: хукаем собственную функцию, системные библиотеки не трогаем.
//  если хук встал, в логе будет: "[Hooks] Dobby <version> self-test OK"
// ============================================================================

static volatile int s_selftest_input = 21;

static __attribute__((noinline)) int selftest_target(int x) { return x * 2; }

static int (*orig_selftest_target)(int) = nullptr;
static int hook_selftest_target(int x) {
    return orig_selftest_target ? orig_selftest_target(x) + 1 : x * 2 + 1;
}

void init() {
    int before = selftest_target(s_selftest_input); // 42
    int rc     = DobbyHook((void *)selftest_target, (void *)hook_selftest_target, (void **)&orig_selftest_target);
    int after  = selftest_target(s_selftest_input); // 43, если хук работает

    bool ok = (rc == 0 && after == before + 1);
    NSLog(@"[Hooks] Dobby %s self-test %s (code=%d, %d -> %d)",
          DobbyGetVersion(), ok ? "OK" : "FAILED", rc, before, after);
}

// ============================================================================
//  игровые хуки
// ============================================================================
//
// Шаблон:
//
//   using Player_Update_t = void (*)(void *player);
//   static Player_Update_t orig_Player_Update;
//   static void hook_Player_Update(void *player) {
//       if (!IL2CPP::ValidPtr((uintptr_t)player)) return;
//       orig_Player_Update(player);   // вызов оригинала
//       // ... своя логика
//   }
//
// Установка в installGameHooks():
//
//   void *klass  = IL2CPP::Class("", "PlayerController");
//   void *method = IL2CPP::Method(klass, "Update", 0);
//   if (method)
//       DobbyHook(method, (void *)hook_Player_Update, (void **)&orig_Player_Update);
//
// ============================================================================

static void installGameHooks() {
    // <<< добавляй хуки сюда >>>
}

static bool s_installed = false;

void tick() {
    if (s_installed || !IL2CPP::Ready()) return;
    s_installed = true;

    installGameHooks();
    NSLog(@"[Hooks] game hooks installed");
}

} // namespace hooks
