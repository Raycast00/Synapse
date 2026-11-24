#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include <float.h>
#include "imgui.h"
#include "imgui_impl_metal.h"

#include "Features/Visuals/visuals.h"
#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Features/Inventory/inventory.h"
#include "Features/Miscellaneous/miscellaneous.h"
#include "Settings/settings.h"

#include "Memory/KittyInclude.hpp"

#include "Render/GlassBackdrop.h"
#include "Render/GlassPanel.h"
#include "Render/GlassStyle.h"
#include "Render/GlassWidgets.h"
#include "Render/GlassPopup.h"     // NEW: попап в отдельном слое

// Fonts
#include "Fonts/NotoSans/Regular.h"

static bool   g_open = false;
static CGRect g_btnRect, g_menuRect;
static bool   g_checkbox = false;

// Демо-состояние для попапа Box: пока ни с чем не связано, заменишь на поля из visuals::
static bool s_demo_fill = false, s_demo_glow = false, s_demo_transparent = false, s_demo_overlay = false;
static bool sel[3] = { false, false, false };
static const char* items[3] = { "Head", "Chest", "Legs" };
static float s_demo_slider = 60.f;

// touch state: клик уходит в imgui через кадр после hover
static ImVec2 g_tPos = ImVec2(-FLT_MAX, -FLT_MAX);
static int    g_tPhase = 0;      // 0 idle, 1 hover frame, 2 send down, 3 held
static bool   g_tRelease = false;
static bool   g_tLeave = false;

// Копии ImDrawData: меню рисуется в основном слое, попап в верхнем
static ImDrawData g_ddMenu, g_ddPopup;

@interface OverlayWindow : UIWindow @end
@implementation OverlayWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    if (self.hidden) return nil;
    // пока открыт попап, ловим все касания: тап мимо закрывает его; иначе только кнопку и окно меню
    bool hit = Glass::PopupCapturesInput() ||
               CGRectContainsPoint(g_btnRect, p) ||
               (g_open && CGRectContainsPoint(g_menuRect, p));
    return hit ? [super hitTest:p withEvent:e] : nil;
}
@end

// Верхний слой: рисует только окно попапа (draw data готовит SystemRenderView)
@interface PopupLayerView : MTKView <MTKViewDelegate>
@property (nonatomic, strong) id<MTLCommandQueue> queue;
@property (nonatomic, assign) ImDrawData *drawData;
@end

@implementation PopupLayerView

- (instancetype)initWithFrame:(CGRect)f device:(id<MTLDevice>)dev {
    self = [super initWithFrame:f device:dev];
    if (!self) return nil;
    self.delegate = self;
    self.queue = [dev newCommandQueue];
    self.clearColor = MTLClearColorMake(0, 0, 0, 0);
    self.opaque = NO;
    self.backgroundColor = UIColor.clearColor;
    ((CAMetalLayer *)self.layer).opaque = NO;
    self.paused = YES;                       // рисуем вручную из SystemRenderView
    self.enableSetNeedsDisplay = NO;
    self.hidden = YES;
    return self;
}

- (void)touchesBegan:(NSSet *)t withEvent:(UIEvent *)e {
    CGPoint p = [[t anyObject] locationInView:self];
    g_tPos = ImVec2(p.x, p.y);
    g_tPhase = 1; g_tRelease = false; g_tLeave = false;
}
- (void)touchesMoved:(NSSet *)t withEvent:(UIEvent *)e {
    CGPoint p = [[t anyObject] locationInView:self];
    g_tPos = ImVec2(p.x, p.y);
}
- (void)touchesEnded:(NSSet *)t withEvent:(UIEvent *)e     { g_tRelease = true; }
- (void)touchesCancelled:(NSSet *)t withEvent:(UIEvent *)e { g_tRelease = true; }

- (void)mtkView:(MTKView *)v drawableSizeWillChange:(CGSize)s {}

- (void)drawInMTKView:(MTKView *)view {
    if (!self.drawData) return;
    MTLRenderPassDescriptor *rpd = view.currentRenderPassDescriptor;
    if (!rpd) return;
    id<MTLCommandBuffer> cb = [self.queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
    ImGui_ImplMetal_RenderDrawData(self.drawData, cb, enc);
    [enc endEncoding];
    [cb presentDrawable:view.currentDrawable];
    [cb commit];
}

@end

static PopupLayerView *g_popupLayer;

@interface SystemRenderView : MTKView <MTKViewDelegate>
@property (nonatomic, strong) id<MTLCommandQueue> queue;
@end

@implementation SystemRenderView

- (instancetype)initWithFrame:(CGRect)f {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    self = [super initWithFrame:f device:dev];
    if (!self) return nil;
    self.delegate = self;
    self.queue = [dev newCommandQueue];
    self.clearColor = MTLClearColorMake(0, 0, 0, 0);
    self.opaque = NO;
    self.backgroundColor = UIColor.clearColor;
    ((CAMetalLayer *)self.layer).opaque = NO;
    self.preferredFramesPerSecond = 60;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Styling
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::GetIO().FontGlobalScale = 1.f;
    ImGui::GetStyle().ScaleAllSizes(1.35f);

    // Rounding shit
    ImGui::GetStyle().FrameRounding = 3.0f;
    ImGui::StyleColorsLight();
    ImGui::GetStyle().WindowRounding = 5.5f;
    ImGui::GetStyle().ScrollbarSize = 14.f;

    ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    ImGui::GetStyle().Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle().Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);

    ImGui::GetStyle().TabBarBorderSize = 0.0f;

    ImVec4 bgColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.97f, 0.97f, 0.97f, 0.95f); 

    ApplyGlassStyle();   // стеклянный стиль, перекрывает цвета и скругления выше

    // Шрифты добавляем один раз. Массивы статические, поэтому atlas не должен их освобождать.
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(Regular, sizeof(Regular), 16.f, &font_cfg);

    ImGui_ImplMetal_Init(dev);
    return self;
}

- (void)touchesBegan:(NSSet *)t withEvent:(UIEvent *)e {
    CGPoint p = [[t anyObject] locationInView:self];
    g_tPos = ImVec2(p.x, p.y);
    g_tPhase = 1; g_tRelease = false; g_tLeave = false;
}
- (void)touchesMoved:(NSSet *)t withEvent:(UIEvent *)e {
    CGPoint p = [[t anyObject] locationInView:self];
    g_tPos = ImVec2(p.x, p.y);
}
- (void)touchesEnded:(NSSet *)t withEvent:(UIEvent *)e     { g_tRelease = true; }
- (void)touchesCancelled:(NSSet *)t withEvent:(UIEvent *)e { g_tRelease = true; }

- (void)mtkView:(MTKView *)v drawableSizeWillChange:(CGSize)s {}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *rpd = view.currentRenderPassDescriptor;
    if (!rpd) return;

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(view.bounds.size.width, view.bounds.size.height);
    CGFloat s = view.window.screen.scale ?: 3.0;
    io.DisplayFramebufferScale = ImVec2(s, s);
    io.DeltaTime = 1.0f / 60.0f;

    // touch -> imgui: hover кадр, потом down, потом up + уход курсора
    io.AddMousePosEvent(g_tPos.x, g_tPos.y);
    if (g_tPhase == 1)      g_tPhase = 2;
    else if (g_tPhase == 2) { io.AddMouseButtonEvent(0, true); g_tPhase = 3; }
    else if (g_tPhase == 3 && g_tRelease) {
        io.AddMouseButtonEvent(0, false);
        g_tRelease = false; g_tPhase = 0; g_tLeave = true;
    }
    else if (g_tLeave) { g_tPos = ImVec2(-FLT_MAX, -FLT_MAX); g_tLeave = false; }

    id<MTLCommandBuffer> cb = [self.queue commandBuffer];
    ImGui_ImplMetal_NewFrame(rpd);
    ImGui::NewFrame();

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    visuals::run();
    aim::run();
    rage::run();
    inventory::run();

    // Кнопка вызова меню (всегда видна)
    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("##btn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 bp = ImGui::GetWindowPos(), bs = ImGui::GetWindowSize();
        float rd = ImGui::GetStyle().WindowRounding;
        Glass::Gloss(bp, ImVec2(bp.x + bs.x, bp.y + bs.y), rd);
        GlassBackdrop_Set(0, CGRectMake(bp.x, bp.y, bs.x, bs.y), rd, true);
    }
    if (ImGui::Button(g_open ? "Close" : "Menu")) {
        g_open = !g_open;
    }
    g_btnRect = CGRectMake(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                           ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGui::End();

    // Само меню
    if (g_open) {
        ImGui::SetNextWindowPos(ImVec2(120, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 350), ImGuiCond_FirstUseEver);

        ImGui::Begin("Synapse iPA", &g_open);

        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        g_menuRect = CGRectInset(CGRectMake(wp.x, wp.y, ws.x, ws.y), -8, -8);

        {
            float rd = ImGui::GetStyle().WindowRounding;
            Glass::Gloss(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), rd);
            GlassBackdrop_Set(1, CGRectMake(wp.x, wp.y, ws.x, ws.y), rd, true);
        }

        if (ImGui::BeginTabBar("Tabs"))
        {
            if (ImGui::BeginTabItem("Visuals"))
            {
                // ENEMY
                Glass::Toggle("Enabled", &visuals::esp_enabled);

                ImGui::SeparatorText("Enemy");

                Glass::Toggle("Box", &visuals::box2D, true);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Box Color", visuals::box_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                // Попап настроек Box (открывается по трём точкам)
                if (Glass::BeginTogglePopup())
                {
                    if (Glass::BeginPopupSection())
                    {
                        Glass::PopupSlider("Exponent", &s_demo_slider, 0.f, 100.f, "%.0f%%");
                        Glass::PopupToggle("Fill", &s_demo_fill);
                        Glass::PopupToggle("Glow", &s_demo_glow);
                        Glass::EndPopupSection();
                    }
                    if (Glass::BeginPopupSection())
                    {
                        Glass::PopupToggle("Transparent", &s_demo_transparent);
                        Glass::PopupToggle("Overlay", &s_demo_overlay);
                        Glass::EndPopupSection();
                    }
                    Glass::EndTogglePopup();
                }

                if (visuals::box2D)
                {
                    ImGui::SameLine();
                    Glass::Toggle("Filled", &visuals::boxFill);
                    ImGui::SameLine();
                    ImGui::ColorEdit4("## Box Fill Color", visuals::box_fill_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                    ImGui::Combo("Box Type", &visuals::current_box_type, visuals::box_type, IM_ARRAYSIZE(visuals::box_type));
                }

                Glass::Toggle("Skeleton", &visuals::skeleton);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Skeleton Color", visuals::skeleton_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                Glass::Toggle("Health Bar", &visuals::healthBar);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Health Bar Color", visuals::healthbar_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                if (visuals::healthBar)
                {
                    ImGui::SameLine();
                    Glass::Toggle("Show Text", &visuals::healthBarText);
                }

                Glass::Toggle("Names", &visuals::names);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Names Color", visuals::names_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                Glass::Toggle("Weapon Text", &visuals::weaponText);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Weapon Text Color", visuals::weapon_text_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                
                Glass::Toggle("Weapon Icon", &visuals::weaponIcon);

                Glass::Toggle("Distance", &visuals::distance);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Distance Color", visuals::distance_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                Glass::Toggle("Tracer", &visuals::tracer);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Tracer Color", visuals::tracer_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::EndTabItem();

                // BOMB
                ImGui::SeparatorText("Bomb");

                Glass::Toggle("Timer", &visuals::bomb_timer_bar);

                // GRENADE
                ImGui::SeparatorText("World");
                Glass::Toggle("Grenade Icons", &visuals::grenade_icons);

                Glass::Toggle("Grenade Trajectory", &visuals::grenade_trajectory);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Trajectory Color", visuals::grenade_trajectory_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                Glass::Toggle("Grenade Area", &visuals::grenade_area);
            }

            if (ImGui::BeginTabItem("Aim"))
            {
                Glass::Toggle("AimBot", &aim::aimbot);
                if (aim::aimbot)
                {
                    ImGui::SameLine();
                    Glass::Toggle("Silent", &aim::silent, true);
                    if (Glass::BeginTogglePopup())
                    {
                        if (Glass::BeginPopupSection())
                        {
                            Glass::PopupToggle("Double Tap", &aim::silent_additional::double_tap);
                            Glass::PopupDropdown("Backtrack", aim::silent_additional::silent_backtrack, aim::silent_additional::silent_backtrack_effort, 4);
                            Glass::PopupDropdown("Remove Spread", aim::silent_additional::silent_spread, aim::silent_additional::silent_spread_effort, 2);
                            Glass::PopupSlider("Extrapolation", &aim::silent_additional::silent_extrapolation, 0.f, 100.f, "%.0f%%");

                            Glass::EndPopupSection();
                        }
                        if (Glass::BeginPopupSection())
                        {
                            Glass::PopupDropdown("Delay Shot", aim::silent_additional::silent_delay, aim::silent_additional::silent_spread_effort, 2, false);

                            Glass::EndPopupSection();
                        }
                        Glass::EndTogglePopup();
                    }
                }
                Glass::Toggle("Automatic Fire", &aim::automatic_fire);

                Glass::Toggle("Draw FOV", &aim::draw_fov);
                ImGui::SameLine();
                ImGui::ColorEdit4("## FOV Color", aim::fov_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::SliderFloat("Field of View", &aim::fov_radius, 15.f, 180.f, "%.0f");

                ImGui::SliderFloat("Smoothing", &aim::aimbot_smoothing, 0.f, 100.f, "%.0f%%");
                ImGui::Combo("Hitboxes", &aim::current_aimbot_hitbox, aim::aimbot_hitboxes, IM_ARRAYSIZE(aim::aimbot_hitboxes));
                
                Glass::Toggle("RCS", &aim::RCS);

                // OTHER
                ImGui::SeparatorText("Other");
                Glass::Toggle("Visualize", &aim::aim_visualize_bullet);

                // ANTI AIM
                ImGui::SeparatorText("Anti Aim");

                Glass::Toggle("Enabled", &aim::anti_aim);
                ImGui::SliderFloat("Pitch", &aim::anti_pitch, -90.f, 180.f, "%.1f");
                ImGui::SliderFloat("Yaw", &aim::anti_yaw, -90.f, 90.f, "%.1f");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rage"))
            {
                ImGui::SeparatorText("Player");

                Glass::Toggle("Move Before Timer", &rage::MBT);
                Glass::Toggle("Wallbang", &rage::wallbang);
                Glass::Toggle("No Fall Damage", &rage::no_fall_damage);
                Glass::Toggle("Air Strafe", &rage::air_strafe);

                ImGui::SeparatorText("Weapon");

                Glass::Toggle("Infinity Ammo", &rage::infinity_ammo);
                Glass::Toggle("Rapid Fire", &rage::rapid_fire);
                Glass::Toggle("No Recoil", &rage::no_recoil);
                Glass::Toggle("No Spread", &rage::no_spread);

                ImGui::SeparatorText("Weapon");

                Glass::Toggle("Auto Win", &rage::auto_win);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Inventory"))
            {
                inventory::draw_tab();

                inventory::AddContainer("Butterfly Knife , test, asdadsadsasdads", "Gamma Dopler Emerald", "Legendary", IM_COL32(255, 196, 64, 255));
                inventory::AddContainer("Butterfly Knife", "Lore",  "Epic",      IM_COL32(160, 110, 255, 255));

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Miscellaneous"))
            {
                Glass::Toggle("Force Thirdperson", &miscellaneous::force_thirdperson, true);
                if (Glass::BeginTogglePopup())
                {
                    if (Glass::BeginPopupSection())
                    {
                        Glass::PopupToggle("Blah blah blah", &miscellaneous::thirdperson_additional::blah);
                        Glass::PopupDropdown("Bleh", sel, items, 3);
                        Glass::EndPopupSection();
                    }
                    if (Glass::BeginPopupSection())
                    {
                        Glass::PopupToggle("Disable Overhead", &miscellaneous::thirdperson_additional::disable_overhead);
                        Glass::EndPopupSection();
                    }
                    Glass::EndTogglePopup();
                }


                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                Glass::Toggle("Watermark", &settings::watermark);
                Glass::Toggle("Overlay", &settings::overlay);
                ImGui::Text("iPA by wurvt");
                ImGui::Text("v0.0.4b");

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            g_menuRect = view.bounds;
    } else {
        g_menuRect = CGRectZero;
        GlassBackdrop_Set(1, CGRectZero, 0, false);
    }

    inventory::end_frame();

    // итог по попапу за этот кадр (до Render)
    Glass::PopupVisual pv = Glass::PopupEndFrame();

    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();

    // Попап уходит в верхний слой: делим список отрисовки на «меню» и «попап»
    bool layered = (pv.visible && pv.draw_list && dd);
    if (layered) {
        g_ddMenu  = *dd;
        g_ddPopup = *dd;
        g_ddMenu.CmdLists.resize(0);  g_ddMenu.CmdListsCount  = 0; g_ddMenu.TotalVtxCount  = 0; g_ddMenu.TotalIdxCount  = 0;
        g_ddPopup.CmdLists.resize(0); g_ddPopup.CmdListsCount = 0; g_ddPopup.TotalVtxCount = 0; g_ddPopup.TotalIdxCount = 0;
        for (int i = 0; i < dd->CmdLists.Size; ++i) {
            ImDrawList *l = dd->CmdLists[i];
            ImDrawData &dst = (l == pv.draw_list || l == pv.draw_list2) ? g_ddPopup : g_ddMenu;
            dst.CmdLists.push_back(l);
            dst.CmdListsCount++;
            dst.TotalVtxCount += l->VtxBuffer.Size;
            dst.TotalIdxCount += l->IdxBuffer.Size;
        }
    }

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
    ImGui_ImplMetal_RenderDrawData(layered ? &g_ddMenu : dd, cb, enc);
    [enc endEncoding];
    [cb presentDrawable:view.currentDrawable];
    [cb commit];

    if (g_popupLayer) {
        if (layered) {
            GlassBackdrop_SetPopup(CGRectMake(pv.pos.x, pv.pos.y, pv.size.x, pv.size.y), pv.rounding, pv.alpha);
            g_popupLayer.drawData = &g_ddPopup;
            g_popupLayer.hidden = NO;
            [g_popupLayer draw];
        } else {
            GlassBackdrop_SetPopup(CGRectZero, 0, 0);
            g_popupLayer.drawData = nullptr;
            g_popupLayer.hidden = YES;
        }
    }
}

@end

static OverlayWindow *g_win;

static void tryInstall(void) {
    if (g_win) return;
    UIWindowScene *scene = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes)
        if ([sc isKindOfClass:UIWindowScene.class] && sc.activationState == UISceneActivationStateForegroundActive)
            { scene = (UIWindowScene *)sc; break; }
    if (!scene) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ tryInstall(); });
        return;
    }

    g_win = [[OverlayWindow alloc] initWithWindowScene:scene];
    g_win.frame = scene.coordinateSpace.bounds;
    g_win.windowLevel = UIWindowLevelNormal + 1;
    g_win.backgroundColor = UIColor.clearColor;

    UIViewController *vc = [UIViewController new];
    UIView *root = [[UIView alloc] initWithFrame:g_win.bounds];
    root.backgroundColor = UIColor.clearColor;

    SystemRenderView *rv = [[SystemRenderView alloc] initWithFrame:root.bounds];
    rv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [root addSubview:rv];
    GlassBackdrop_Install(root, rv);        // блюр-подложки меню лежат под основным MTKView
    GlassBackdrop_InstallPopup(root, rv);   // блюр попапа лежит над основным MTKView

    PopupLayerView *pl = [[PopupLayerView alloc] initWithFrame:root.bounds device:rv.device];
    pl.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [root addSubview:pl];                   // самый верхний слой: только попап
    g_popupLayer = pl;

    vc.view = root;
    g_win.rootViewController = vc;
    g_win.hidden = NO;
}

static inline const char* IL2CPP_FRAMEWORK(const char* NAME) {
    NSString *appPath = [[NSBundle mainBundle] bundlePath];
    NSString *binaryPath = [NSString stringWithFormat:@"%@/Frameworks/%@.framework/%@",
                            appPath,
                            [NSString stringWithUTF8String:NAME],
                            [NSString stringWithUTF8String:NAME]];
    return [binaryPath UTF8String];
}

__attribute__((constructor)) static void init(void) {
    tryInstall();
}