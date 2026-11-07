#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include <float.h>
#include "imgui.h"
#include "imgui_impl_metal.h"

#include "Features/Visuals/visuals.h"
#include "Features/Aim/aim.h"
#include "Features/Rage/rage.h"
#include "Settings/settings.h"

#include "Memory/KittyInclude.hpp"

static bool   g_open = false;
static CGRect g_btnRect, g_menuRect;
static bool   g_checkbox = false;

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

    // Styling
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::GetIO().FontGlobalScale = 0.95f;
    ImGui::GetStyle().ScaleAllSizes(1.0f);

    // Rounding shit
    ImGui::GetStyle().FrameRounding = 3.0f;
    ImGui::StyleColorsLight();
    ImGui::GetStyle().WindowRounding = 3.0f;
    ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

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

    //visuals::run();
    aim::run();
    //rage::run();

    // Кнопка вызова меню (всегда видна)
    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("##btn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::Button(g_open ? "Close" : "Menu")) {
        g_open = !g_open;
    }
    g_btnRect = CGRectMake(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                           ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGui::End();

    // Само меню
    if (g_open) {
        ImGui::SetNextWindowPos(ImVec2(120, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);

        ImGui::Begin("Synapse iPA", &g_open);

        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        g_menuRect = CGRectInset(CGRectMake(wp.x, wp.y, ws.x, ws.y), -8, -8);

        if (ImGui::BeginTabBar("Tabs"))
        {
            if (ImGui::BeginTabItem("Visuals"))
            {
                // ENEMY
                ImGui::Text("Esp player");
                ImGui::Checkbox("Enabled", &visuals::esp_enabled);

                ImGui::SeparatorText("Configuration");

                ImGui::Checkbox("2D Box", &visuals::box2D);
                ImGui::SameLine();
                ImGui::ColorEdit4("## 2D Box Color", visuals::box_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                ImGui::Checkbox("Box Fill", &visuals::boxFill);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Box Fill Color", visuals::box_fill_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                ImGui::Checkbox("Health Bar", &visuals::healthBar);
                ImGui::SameLine();
                ImGui::ColorEdit4("## Health Bar Color", visuals::healthbar_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

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
            }

            if (ImGui::BeginTabItem("Aim"))
            {
                ImGui::Checkbox("AimBot", &aim::aimbot);

                ImGui::Checkbox("Draw FOV", &aim::drawFOV);
                ImGui::SameLine();
                ImGui::ColorEdit4("## FOV Color", aim::fov_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::SliderFloat("FOV Radius", &aim::fov_radius, 60.f, 180.f, "%.0f");

                ImGui::Checkbox("RCS", &aim::RCS);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rage"))
            {
                ImGui::Checkbox("No Spread", &rage::noSpread);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                ImGui::Text("Settings");
                ImGui::Checkbox("Watermark", &settings::watermark);
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

__attribute__((constructor)) static void init(void) {
    tryInstall();
}