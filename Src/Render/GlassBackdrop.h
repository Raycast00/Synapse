#pragma once
#import <UIKit/UIKit.h>

// host: контейнер, в котором лежит MTKView. below: сам MTKView (блюр вставляется ниже него).
void GlassBackdrop_Install(UIView *host, UIView *below);
// slot: 0 = кнопка меню, 1 = само меню. Координаты в точках, те же, что у ImGui.
void GlassBackdrop_Set(int slot, CGRect frame, CGFloat radius, bool visible);