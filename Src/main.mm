#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#include <float.h>
#include "imgui.h"
#include "imgui_impl_metal.h"

#include "Menu/menu.h"

#include "Features/Visuals/visuals.h"
#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Features/Misc/misc.h"

#include "Settings/settings.h"

#include "Memory/KittyInclude.hpp"

static bool   g_open = false;
static CGRect g_btnRect = CGRectZero, g_menuRect = CGRectZero;

// touch state: клик уходит в imgui через кадр после hover
static ImVec2 g_tPos = ImVec2(-FLT_MAX, -FLT_MAX);
static int    g_tPhase = 0;      // 0 idle, 1 hover frame, 2 send down, 3 held
static bool   g_tRelease = false;
static bool   g_tLeave = false;

static double g_lastFrame = 0.0;

@interface OverlayWindow : UIWindow @end
@implementation OverlayWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    if (self.hidden) return nil;
    // ловим касания только по кнопке и окну меню, остальное идёт в игру
    bool hit = CGRectContainsPoint(g_btnRect, p) || (g_open && CGRectContainsPoint(g_menuRect, p));
    return hit ? [super hitTest:p withEvent:e] : nil;
}
@end

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

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Встроенные шрифты и стиль Neverlose-меню.
    menu::init();
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

    // Настоящее время кадра: анимации меню идут с частотой экрана.
    const double now = CACurrentMediaTime();
    if (g_lastFrame > 0.0) {
        double delta = now - g_lastFrame;
        if (delta < 1.0 / 240.0) delta = 1.0 / 240.0;
        if (delta > 1.0 / 15.0) delta = 1.0 / 15.0;
        io.DeltaTime = (float)delta;
    } else {
        io.DeltaTime = 1.0f / 60.0f;
    }
    g_lastFrame = now;

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

    visuals::run();
    aim::run();
    rage::run();
    misc::run();

    // Keep the menu inside the notch/home-indicator safe area on every iPhone.
    const UIEdgeInsets safe = view.safeAreaInsets;
    const float margin = 10.0f;

    if (g_open) {
        // The nlka canvas is 750x756: scale it uniformly to fit the safe area.
        const float avail_w = view.bounds.size.width - (float)safe.left - (float)safe.right - margin * 2.0f;
        const float avail_h = view.bounds.size.height - (float)safe.top - (float)safe.bottom - margin * 2.0f;
        float scale = avail_w / 750.0f;
        const float scale_y = avail_h / 756.0f;
        if (scale_y < scale) scale = scale_y;
        if (scale > 1.0f) scale = 1.0f;

        const ImVec2 menuOrigin((float)safe.left + margin + (avail_w - 750.0f * scale) * 0.5f,
                                (float)safe.top + margin + (avail_h - 756.0f * scale) * 0.5f);

        menu::render(&g_open, menuOrigin, scale);

        if (ImGui::BeginTabBar("Tabs"))
        {
            if (ImGui::BeginTabItem("Visuals"))
            {
                // ENEMY
                ImGui::Checkbox("Enabled", &visuals::esp_enabled);

                ImGui::SeparatorText("Enemy");

                ImGui::Checkbox("Box", &visuals::box2D);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Box Color", visuals::box_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                if (visuals::box2D)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Filled", &visuals::boxFill);
                    ImGui::SameLine();
                    ImGui::ColorEdit4("## Box Fill Color", visuals::box_fill_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                    ImGui::Combo("Box Type", &visuals::current_box_type, visuals::box_type, IM_ARRAYSIZE(visuals::box_type));
                }

                ImGui::Checkbox("Skeleton", &visuals::skeleton);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Skeleton Color", visuals::skeleton_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                ImGui::Checkbox("Health Bar", &visuals::healthBar);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Health Bar Color", visuals::healthbar_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                if (visuals::healthBar)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Show Text", &visuals::healthBarText);
                }

                ImGui::Checkbox("Names", &visuals::names);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Names Color", visuals::names_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                ImGui::Checkbox("Weapon Text", &visuals::weaponText);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Weapon Text Color", visuals::weapon_text_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                
                ImGui::Checkbox("Weapon Icon", &visuals::weaponIcon);

                ImGui::Checkbox("Distance", &visuals::distance);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Distance Color", visuals::distance_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                ImGui::Checkbox("Tracer", &visuals::tracer);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Tracer Color", visuals::tracer_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::EndTabItem();

                // BOMB
                ImGui::SeparatorText("Bomb");

                ImGui::Checkbox("Timer", &visuals::bomb_timer_bar);

                // GRENADE
                ImGui::SeparatorText("Grenade");

                ImGui::Checkbox("Visualize Trajectory", &visuals::visualize_grenade_trajectory);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Trajectory Color", visuals::grenade_trajectory_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            }

            if (ImGui::BeginTabItem("Aim"))
            {
                ImGui::Checkbox("AimBot", &aim::aimbot);
                if (aim::aimbot)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Silent", &aim::silent);
                }
                ImGui::Checkbox("Aim Through Walls", &aim::aim_through_walls);
                ImGui::Checkbox("Automatic Fire", &aim::automatic_fire);

                ImGui::Checkbox("Draw FOV", &aim::draw_fov);
                ImGui::SameLine();
                ImGui::ColorEdit4("## FOV Color", aim::fov_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::SliderFloat("Field of View", &aim::fov_radius, 15.f, 180.f, "%.0f");

                ImGui::SliderFloat("Smoothing", &aim::aimbot_smoothing, 0.f, 100.f, "%.0f%%");
                ImGui::Combo("Hitboxes", &aim::current_aimbot_hitbox, aim::aimbot_hitboxes, IM_ARRAYSIZE(aim::aimbot_hitboxes));
                
                ImGui::Checkbox("RCS", &aim::RCS);

                // OTHER
                ImGui::SeparatorText("Other");
                ImGui::Checkbox("Visualize", &aim::aim_visualize_bullet);

                // ANTI AIM
                ImGui::SeparatorText("Anti Aim");

                ImGui::Checkbox("Enabled", &aim::anti_aim);
                ImGui::SliderFloat("Pitch", &aim::anti_pitch, -90.f, 180.f, "%.1f");
                ImGui::SliderFloat("Yaw", &aim::anti_yaw, -90.f, 90.f, "%.1f");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rage"))
            {
                ImGui::SeparatorText("Player");

                ImGui::Checkbox("Move Before Timer", &rage::MBT);

                ImGui::Checkbox("No Clip", &rage::flight);
                if (rage::flight)
                {
                    ImGui::SliderFloat("Speed", &rage::flight_speed, 1.f, 20.f, "%.1f");
                }

                ImGui::Checkbox("Wallbang", &rage::wallbang);
                ImGui::Checkbox("No Fall Damage", &rage::no_fall_damage);

                ImGui::SeparatorText("Weapon");

                ImGui::Checkbox("Infinity Ammo", &rage::infinity_ammo);
                ImGui::Checkbox("Rapid Fire", &rage::rapid_fire);
                ImGui::Checkbox("No Recoil", &rage::no_recoil);
                ImGui::Checkbox("No Spread", &rage::no_spread);

                ImGui::SeparatorText("Game");

                ImGui::Checkbox("Auto Kill", &rage::auto_kill);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Misc"))
            {
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                ImGui::Text("Settings");
                ImGui::Checkbox("Watermark", &settings::watermark);
                ImGui::Checkbox("Overlay", &settings::overlay);
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
        const ImVec2 launchPos(margin + (float)safe.left, margin + (float)safe.top);
        const ImVec2 launchSize(84.0f, 34.0f);

        menu::launcher(&g_open, launchPos, launchSize);

        g_btnRect = CGRectMake(launchPos.x, launchPos.y, launchSize.x, launchSize.y);
        g_menuRect = CGRectZero;
    }
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        g_menuRect = view.bounds;

    ImGui::Render();
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
    [enc endEncoding];
    [cb presentDrawable:view.currentDrawable];
    [cb commit];
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
    vc.view = [[SystemRenderView alloc] initWithFrame:g_win.bounds];
    vc.view.backgroundColor = UIColor.clearColor;
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
