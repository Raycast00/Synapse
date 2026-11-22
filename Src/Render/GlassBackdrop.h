#pragma once
#import <UIKit/UIKit.h>

// host: контейнер, в котором лежат MTKView. below: основной MTKView (блюр вставляется ниже него).
void GlassBackdrop_Install(UIView *host, UIView *below);
// slot: 0 = кнопка меню, 1 = само меню, 2 = вьюер инвентаря. Координаты в точках, те же, что у ImGui.
void GlassBackdrop_Set(int slot, CGRect frame, CGFloat radius, bool visible);

// Блюр попапа: ложится ВЫШЕ основного MTKView (чтобы размывать и виджеты меню), но ниже слоя попапа.
void GlassBackdrop_InstallPopup(UIView *host, UIView *above);
void GlassBackdrop_SetPopup(CGRect frame, CGFloat radius, CGFloat alpha);