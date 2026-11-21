#import "GlassBackdrop.h"

static const int kSlots = 2;
static UIVisualEffectView *g_views[kSlots];
static CAGradientLayer    *g_grads[kSlots];

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
            (id)[UIColor colorWithRed:0.55 green:0.35 blue:1.00 alpha:0.20].CGColor,
            (id)[UIColor colorWithRed:0.20 green:0.30 blue:0.90 alpha:0.08].CGColor
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