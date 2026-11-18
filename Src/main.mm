#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include <float.h>
#include "imgui.h"
#include "imgui_impl_metal.h"

#include "Menu/menu.hpp"

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

    ImGui::GetIO().IniFilename = nullptr;

    // neverlose-style theme: embedded fonts + palette + style
    menu::initialize();

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

    visuals::run();
    aim::run();
    rage::run();
    misc::run();

    UIEdgeInsets safe = view.safeAreaInsets;
    menu::setSafeInsets((float)safe.top, (float)safe.bottom);

    // Launcher + shell. begin() always draws the launcher when closed.
    if (menu::begin(g_open))
    {
        if (menu::page(menu::Page::Visuals))
        {
            menu::card("PLAYER");
            menu::toggle("Enabled", &visuals::esp_enabled);
            menu::toggleColor("Box", &visuals::box2D, visuals::box_color);
            menu::toggleColor("Filled Box", &visuals::boxFill, visuals::box_fill_color);
            menu::combo("Box Type", &visuals::current_box_type,
                        visuals::box_type, IM_ARRAYSIZE(visuals::box_type), visuals::box2D);
            menu::toggleColor("Skeleton", &visuals::skeleton, visuals::skeleton_color);
            menu::toggleColor("Health Bar", &visuals::healthBar, visuals::healthbar_color);
            menu::toggle("Health Text", &visuals::healthBarText, visuals::healthBar);
            menu::toggleColor("Names", &visuals::names, visuals::names_color);
            menu::toggleColor("Weapon Text", &visuals::weaponText, visuals::weapon_text_color);
            menu::toggle("Weapon Icon", &visuals::weaponIcon);
            menu::toggleColor("Distance", &visuals::distance, visuals::distance_color);
            menu::toggleColor("Tracer", &visuals::tracer, visuals::tracer_color);
            menu::endCard();

            menu::card("BOMB");
            menu::toggleColor("Timer Bar", &visuals::bomb_timer_bar, visuals::bomb_timer_color);
            menu::endCard();

            menu::card("GRENADE");
            menu::toggleColor("Trajectory", &visuals::visualize_grenade_trajectory,
                              visuals::grenade_trajectory_color);
            menu::endCard();
        }

        if (menu::page(menu::Page::Aim))
        {
            menu::card("AIMBOT");
            menu::toggle("Enabled", &aim::aimbot);
            menu::toggle("Silent", &aim::silent, aim::aimbot);
            menu::toggle("Automatic Fire", &aim::automatic_fire);
            menu::combo("Hitbox", &aim::current_aimbot_hitbox,
                        aim::aimbot_hitboxes, IM_ARRAYSIZE(aim::aimbot_hitboxes));
            menu::slider("Field of View", &aim::fov_radius, 15.0f, 180.0f, "%.0f\xc2\xb0");
            menu::slider("Smoothing", &aim::aimbot_smoothing, 0.0f, 100.0f, "%.0f%%");
            menu::toggleColor("Draw FOV", &aim::draw_fov, aim::fov_color);
            menu::endCard();

            menu::card("WEAPON");
            menu::toggle("Recoil Control", &aim::RCS);
            menu::toggle("Visualize Bullet", &aim::aim_visualize_bullet);
            menu::endCard();

            menu::card("ANTI AIM");
            menu::toggle("Enabled", &aim::anti_aim);
            menu::slider("Pitch", &aim::anti_pitch, -90.0f, 180.0f, "%.1f", aim::anti_aim);
            menu::slider("Yaw", &aim::anti_yaw, -90.0f, 90.0f, "%.1f", aim::anti_aim);
            menu::endCard();
        }

        if (menu::page(menu::Page::Rage))
        {
            menu::card("PLAYER");
            menu::toggle("Move Before Timer", &rage::MBT);
            menu::toggle("No Clip", &rage::flight);
            menu::slider("Speed", &rage::flight_speed, 1.0f, 20.0f, "%.1f", rage::flight);
            menu::toggle("No Fall Damage", &rage::no_fall_damage);
            menu::toggle("Wallbang", &rage::wallbang);
            menu::endCard();

            menu::card("WEAPON");
            menu::toggle("Infinity Ammo", &rage::infinity_ammo);
            menu::toggle("Rapid Fire", &rage::rapid_fire);
            menu::toggle("Perfect Accuracy", &rage::perfect_accuracy);
            menu::toggle("No Recoil", &rage::no_recoil);
            menu::toggle("No Spread", &rage::no_spread);
            menu::endCard();

            menu::card("BOMB");
            menu::toggle("Fast Defuse", &rage::fast_defuse);
            menu::toggle("Auto Win", &rage::auto_win);
            menu::endCard();
        }

        if (menu::page(menu::Page::Misc))
        {
            menu::card("MISCELLANEOUS");
            menu::text("Nothing here yet.");
            menu::caption("More features will be added later.");
            menu::endCard();
        }

        if (menu::page(menu::Page::Settings))
        {
            menu::card("INTERFACE");
            menu::toggle("Watermark", &settings::watermark);
            menu::toggle("Overlay", &settings::overlay);
            menu::endCard();

            menu::card("ABOUT");
            menu::text("Synapse for iOS");
            menu::caption("neverlose theme  \xc2\xb7  v0.0.4b");
            menu::caption("iPA by wurvt");
            menu::endCard();
        }

        menu::end();
    }

    // Keep the touch catcher in sync with what is actually drawn.
    const ImVec4 btn = menu::buttonRect();
    g_btnRect = CGRectMake(btn.x, btn.y, btn.z, btn.w);

    if (g_open)
    {
        if (menu::popupOpen())
        {
            g_menuRect = view.bounds; // popups may stick out of the shell
        }
        else
        {
            const ImVec4 shell = menu::shellRect();
            g_menuRect = CGRectMake(shell.x, shell.y, shell.z, shell.w);
        }
    }
    else
    {
        g_menuRect = CGRectZero;
    }

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
