#pragma once

namespace hooks {

// вызывается один раз при загрузке дилб (self-test Dobby)
void init();

// вызывается каждый кадр из рендер-лупа: ставит игровые хуки, когда il2cpp готов
void tick();

} // namespace hooks
