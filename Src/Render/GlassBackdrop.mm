#import "GlassBackdrop.h"

static const int kSlots = 3;
static UIVisualEffectView *g_views[kSlots];
static CAGradientLayer    *g_grads[kSlots];
static UIVisualEffectView *g_popupView;

void GlassBackdrop_Install(UIView *host, UIView *below) {
    for (int i = 0; i < kSlots; i++) {
        UIBlurEffect *fx = [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemUltraThinMaterialDark];
        UIVisualEffectView *v = [[UIVisualEffectView alloc] initWithEffect:fx];
        v.userInteractionEnabled = NO;          // касания должны доходить до MTKView
        v.clipsToBounds = YES;
        v.layer.cornerCurve = kCACornerCurveContinuous;
        v.hidden = YES;

        CAGradientLayer *g = [CAGradientLayer layer];
        g.colors = @[
            (id)[UIColor colorWithWhite:1.0 alpha:0.10].CGColor,
            (id)[UIColor colorWithWhite:1.0 alpha:0.03].CGColor
        ];
        g.startPoint = CGPointMake(0.15, 0.0);
        g.endPoint   = CGPointMake(0.85, 1.0);
        [v.contentView.layer addSublayer:g];

        [host insertSubview:v belowSubview:below];
        g_views[i] = v;
        g_grads[i] = g;
    }
}

void GlassBackdrop_Set(int slot, CGRect f, CGFloat r, bool visible) {
    if (slot < 0 || slot >= kSlots || !g_views[slot]) return;
    UIVisualEffectView *v = g_views[slot];
    if (!visible || f.size.width < 1 || f.size.height < 1) { v.hidden = YES; return; }

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    v.hidden = NO;
    if (!CGRectEqualToRect(v.frame, f)) {
        v.frame = f;
        g_grads[slot].frame = v.bounds;
    }
    if (v.layer.cornerRadius != r) v.layer.cornerRadius = r;
    [CATransaction commit];
}

void GlassBackdrop_InstallPopup(UIView *host, UIView *above) {
    UIBlurEffect *fx = [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemThinMaterialDark];
    g_popupView = [[UIVisualEffectView alloc] initWithEffect:fx];
    g_popupView.userInteractionEnabled = NO;
    g_popupView.clipsToBounds = YES;
    g_popupView.layer.cornerCurve = kCACornerCurveContinuous;
    g_popupView.hidden = YES;
    [host insertSubview:g_popupView aboveSubview:above];
}

void GlassBackdrop_SetPopup(CGRect f, CGFloat r, CGFloat alpha) {
    if (!g_popupView) return;
    if (alpha < 0.01 || f.size.width < 1 || f.size.height < 1) { g_popupView.hidden = YES; return; }

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    g_popupView.hidden = NO;
    g_popupView.alpha = alpha;                    // плавное появление и исчезновение блюра
    if (!CGRectEqualToRect(g_popupView.frame, f)) g_popupView.frame = f;
    if (g_popupView.layer.cornerRadius != r) g_popupView.layer.cornerRadius = r;
    [CATransaction commit];
}