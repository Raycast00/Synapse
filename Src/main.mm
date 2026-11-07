#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include "imgui.h"
#include "imgui_impl_metal.h"

#include "Features/Visuals/visuals.h"

static bool   g_open = false;
static CGRect g_btnRect, g_menuRect;
static bool   g_checkbox = false;

@interface OverlayWindow : UIWindow @end
@implementation OverlayWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e {
    if (self.hidden) return nil;
    
    // Если меню открыто — блокируем игру полностью (все касания идут в ImGui)
    if (g_open) {
        return [super hitTest:p withEvent:e];
    }
    
    // Если меню закрыто — пропускаем касания в игру, КРОМЕ области кнопки "Menu"
    bool hit = CGRectContainsPoint(g_btnRect, p);
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

    ImGui::GetIO().FontGlobalScale = 1.0f;
    ImGui::GetStyle().ScaleAllSizes(0.5f);

    // Rounding shit
    ImGui::GetStyle().FrameRounding = 3.0f;
    ImGui::StyleColorsLight();
    ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

    ImGui_ImplMetal_Init(dev);
    return self;
}

- (void)touch:(NSSet *)t down:(BOOL)down {
    UITouch *touch = [t anyObject];
    CGPoint p = [touch locationInView:self];
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    io.AddMouseButtonEvent(0, down);
}

- (void)touchesBegan:(NSSet *)t withEvent:(UIEvent *)e     { [self touch:t down:YES]; }
- (void)touchesMoved:(NSSet *)t withEvent:(UIEvent *)e     { [self touch:t down:YES]; }
- (void)touchesEnded:(NSSet *)t withEvent:(UIEvent *)e     { [self touch:t down:NO]; }
- (void)touchesCancelled:(NSSet *)t withEvent:(UIEvent *)e { [self touch:t down:NO]; }

- (void)mtkView:(MTKView *)v drawableSizeWillChange:(CGSize)s {}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *rpd = view.currentRenderPassDescriptor;
    if (!rpd) return;

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(view.bounds.size.width, view.bounds.size.height);
    CGFloat s = view.window.screen.scale ?: 3.0;
    io.DisplayFramebufferScale = ImVec2(s, s);
    io.DeltaTime = 1.0f / 60.0f;

    id<MTLCommandBuffer> cb = [self.queue commandBuffer];
    ImGui_ImplMetal_NewFrame(rpd);
    ImGui::NewFrame();

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
        ImGui::SetNextWindowSize(ImVec2(260, 160), ImGuiCond_FirstUseEver);

        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            ImGui::SetNextWindowFocus();
        }

        ImGui::Begin("Synapse IPA", &g_open);

        if (ImGui::BeginTabBar("Tabs"))
        {
            if (ImGui::BeginTabItem("Visuals"))
            {
                ImGui::Text("Esp player");
                ImGui::Checkbox("2D Box", &visuals::box2D);
                ImGui::Checkbox("Box Fill", &visuals::boxFill);
                ImGui::Checkbox("Health Bar", &visuals::healthBar);
                ImGui::Checkbox("Names", &visuals::names);
                ImGui::Checkbox("Weapon Text", &visuals::weaponText);
                ImGui::Checkbox("Weapon Icon", &visuals::weaponIcon);
                ImGui::Checkbox("Distance", &visuals::distance);
                ImGui::Checkbox("Tracer", &visuals::tracer);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Aim"))
            {
                ImGui::Text("AimBot");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                ImGui::Text("Settings");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
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