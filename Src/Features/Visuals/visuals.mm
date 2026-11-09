#include "visuals.h"
#include "il2cpp/il2cpp.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace il = IL2CPP;

namespace
{
    struct V3     { float x, y, z; };
    struct Bounds { V3 center; V3 extents; };

    constexpr bool kStatus = true;   // строка диагностики на экране; false = выключить

    // Источник оффсетов: dump.cs, присланный пользователем (Assembly-CSharp, PhotonRealtime).
    namespace Cfg
    {
        constexpr uintptr_t PC_IsRagdolling = 0x49;  // PlayerController.IsRagdolling
        constexpr uintptr_t PC_MainCamera   = 0x30;  // PlayerController._mainCamera
        constexpr uintptr_t PC_Player       = 0x90;  // PlayerController._player
        constexpr uintptr_t PC_IsDeath      = 0xA8;  // PlayerController._isDeath
        constexpr uintptr_t Player_IsLocal  = 0x1C;  // Photon.Realtime.Player.IsLocal
        constexpr uintptr_t Player_Nick     = 0x20;  // Photon.Realtime.Player.nickName
        constexpr uintptr_t Skel_Body       = 0x60;  // PlayerSkeleton._body
        constexpr uintptr_t Skel_BodyLod    = 0x68;  // PlayerSkeleton._bodyLod
        constexpr uintptr_t Bomb_IsActive   = 0x18;  // Bomb._isActive

        constexpr double RefreshInterval = 0.20;
        constexpr float  MaxHealth       = 100.f;    // не проверено по дампу

        constexpr size_t MaxSourceBones = 512;
        constexpr size_t PickLow = 3, PickHigh = 3, PickSide = 6, PickStride = 8;
        constexpr float  PadTop    = 0.08f;
        constexpr float  PadBottom = 0.04f;
        constexpr float  PadSide   = 0.05f;
        constexpr float  OutlierK  = 3.5f;   // при выборе: кость дальше K * медианной дистанции отбрасывается
        constexpr float  ReachK    = 1.6f;
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPlayerController{}, *kPlayer{}, *kSkeleton{}, *kBomb{}, *kSMR{};
        void *mGetTransform{}, *mGetPosition{}, *mW2S{}, *mCamMain{};
        void *mGetBounds{}, *mGetCompInChildren{}, *mObjGetName{}, *mGetBones{}, *mGetHeadBone{};
        void *mScreenW{}, *mScreenH{};
        void *mPcHealth{}, *mPlayerTeam{}, *mPlayerIsAlive{}, *mSkelWeapon{}, *mBombTimeLeft{};
    } A;

    bool ResolveApi()
    {
        if (A.ready) return true;
        A.missing.clear();
        const char* core = "UnityEngine.CoreModule";
        auto need = [&](void* p, const char* n) { if (!p) { A.missing += n; A.missing += ' '; } };

        A.kPlayerController = il::Class("", "PlayerController", "Assembly-CSharp");
        A.kSkeleton         = il::Class("ScarletWork.Game", "PlayerSkeleton", "Assembly-CSharp");
        A.kBomb             = il::Class("ScarletWork.Game", "Bomb", "Assembly-CSharp");
        A.kPlayer           = il::Class("Photon.Realtime", "Player", "PhotonRealtime");

        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);
        void* kCamera    = il::Class("UnityEngine", "Camera", core);
        void* kRenderer  = il::Class("UnityEngine", "Renderer", core);
        void* kScreen    = il::Class("UnityEngine", "Screen", core);
        void* kObject    = il::Class("UnityEngine", "Object", core);
        A.kSMR           = il::Class("UnityEngine", "SkinnedMeshRenderer", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2);
        A.mGetPosition       = il::Method(kTransform, "get_position", 0);
        A.mW2S               = il::Method(kCamera, "WorldToScreenPoint", 1);
        A.mCamMain           = il::Method(kCamera, "get_main", 0);
        A.mGetBounds         = il::Method(kRenderer, "get_bounds", 0);
        A.mGetBones          = il::Method(A.kSMR, "get_bones", 0);          // опционально: без него откат на bounds
        A.mScreenW           = il::Method(kScreen, "get_width", 0);
        A.mScreenH           = il::Method(kScreen, "get_height", 0);
        A.mObjGetName        = il::Method(kObject, "get_name", 0);
        A.mPcHealth          = il::Method(A.kPlayerController, "get_Health", 0);
        A.mPlayerTeam        = il::Method(A.kPlayer, "get_Team", 0);
        A.mPlayerIsAlive     = il::Method(A.kPlayer, "get_IsAlive", 0);
        A.mSkelWeapon        = il::Method(A.kSkeleton, "get_Weapon", 0);
        A.mGetHeadBone       = il::Method(A.kSkeleton, "GetHeadBone", 0);   // опционально
        A.mBombTimeLeft      = il::Method(A.kBomb, "get_TimeLeft", 0);

        need(A.kPlayerController, "PlayerController"); need(A.kSkeleton, "PlayerSkeleton");
        need(kComponent, "Component"); need(kTransform, "Transform"); need(kCamera, "Camera");
        need(kRenderer, "Renderer"); need(kScreen, "Screen");
        need(A.mGetTransform, "get_transform"); need(A.mGetCompInChildren, "GetComponentInChildren/2");
        need(A.mGetPosition, "get_position"); need(A.mW2S, "WorldToScreenPoint");
        need(A.mGetBounds, "get_bounds"); need(A.mScreenW, "get_width"); need(A.mScreenH, "get_height");

        A.ready = A.missing.empty();
        return A.ready;
    }

    std::string Str(void* s)
    {
        uintptr_t p = (uintptr_t)s;
        if (!il::ValidPtr(p)) return {};
        int32_t len = il::Read<int32_t>(p + 0x10);
        if (len <= 0 || len > 64) return {};
        std::string out;
        for (int32_t i = 0; i < len; i++) {
            uint16_t c = il::Read<uint16_t>(p + 0x14 + i * 2);
            if (c < 0x80)       out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else                { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        }
        return out;
    }

    bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
                                      std::fabs(v.x) < 1e6f && std::fabs(v.y) < 1e6f && std::fabs(v.z) < 1e6f; }

    // Вызов геттера с boxed-результатом. false, если вызов не удался (исключение или нет метода).
    bool CallInt(void* m, void* inst, int& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *(int*)((uint8_t*)r + 0x10);
        return true;
    }
    bool CallBool(void* m, void* inst, bool& out)
    {
        void* r = il::Invoke(m, inst, nullptr);
        if (!il::ValidPtr((uintptr_t)r)) return false;
        out = *((uint8_t*)r + 0x10) != 0;
        return true;
    }

    V3 WorldPos(void* component)
    {
        void* tr = il::Invoke(A.mGetTransform, component, nullptr);
        if (!il::ValidPtr((uintptr_t)tr)) return {0, 0, 0};
        return il::InvokeBoxed<V3>(A.mGetPosition, tr, nullptr);
    }

    struct Ent
    {
        uintptr_t pc = 0, player = 0, skeleton = 0, body = 0;
        std::vector<uintptr_t> bones;   // Transform* костей скина (до MaxBones)
        float reach = 0.f;
        std::string name, team, weapon;
    };

    struct Stats
    {
        int pc = 0, local = 0, noPlayer = 0, noSkel = 0, noBody = 0, withBones = 0;
        int boundsBad = 0, behind = 0, tiny = 0, drawn = 0, teamSkipped = 0, dead = 0, fallback = 0;
    } S;

    struct Frame
    {
        std::vector<Ent> ents;
        uintptr_t camera = 0;
        V3        localPos{};
        bool      haveLocal = false;
        std::string localTeam;
        uintptr_t bomb = 0;
        double    lastRefresh = -1.0;
    } F;

    void CollectBones(Ent& e)
    {
        e.bones.clear();
        if (!A.mGetBones) return;
        uintptr_t arr = (uintptr_t)il::Invoke(A.mGetBones, (void*)e.body, nullptr);
        if (!il::ValidPtr(arr)) return;
        uint64_t len = il::Read<uint64_t>(arr + 0x18);           // Il2CppArray: max_length @0x18, data @0x20
        if (len == 0 || len > 512) return;

        std::vector<uintptr_t> all;
        for (uint64_t i = 0; i < len; i++) {
            uintptr_t t = il::Read<uintptr_t>(arr + 0x20 + i * 8);
            if (il::ValidPtr(t)) all.push_back(t);
        }
        if (all.size() < 4) return;

        size_t stride = (all.size() + Cfg::MaxBones - 1) / Cfg::MaxBones;
        for (size_t i = 0; i < all.size(); i += stride) e.bones.push_back(all[i]);
    }

    void Refresh()
    {
        F.ents.clear();
        F.camera = 0; F.haveLocal = false; F.localTeam.clear(); F.bomb = 0;
        S.pc = S.local = S.noPlayer = S.noSkel = S.noBody = S.withBones = 0;

        for (uintptr_t pc : il::FindObjectsOfType(A.kPlayerController)) {
            S.pc++;
            uintptr_t player = il::Read<uintptr_t>(pc + Cfg::PC_Player);
            if (!il::ValidPtr(player)) { S.noPlayer++; continue; }

            std::string team = Str(il::Invoke(A.mPlayerTeam, (void*)player, nullptr));

            if (il::Read<uint8_t>(player + Cfg::Player_IsLocal)) {
                S.local++;
                uintptr_t cam = il::Read<uintptr_t>(pc + Cfg::PC_MainCamera);
                if (il::ValidPtr(cam)) F.camera = cam;
                F.localPos  = WorldPos((void*)pc);
                F.localTeam = team;
                F.haveLocal = true;
                continue;
            }

            Ent e;
            e.pc     = pc;
            e.player = player;
            e.team   = team;
            e.name   = Str((void*)il::Read<uintptr_t>(player + Cfg::Player_Nick));

            bool inc = true;
            void* a[2] = { il::Type(A.kSkeleton), &inc };
            e.skeleton = (uintptr_t)il::Invoke(A.mGetCompInChildren, (void*)pc, a);
            if (!il::ValidPtr(e.skeleton)) { S.noSkel++; continue; }

            e.body = il::Read<uintptr_t>(e.skeleton + Cfg::Skel_Body);
            if (!il::ValidPtr(e.body)) e.body = il::Read<uintptr_t>(e.skeleton + Cfg::Skel_BodyLod);
            if (!il::ValidPtr(e.body)) { S.noBody++; continue; }

            CollectBones(e);
            if (!e.bones.empty()) S.withBones++;

            void* wgo = il::Invoke(A.mSkelWeapon, (void*)e.skeleton, nullptr);
            if (il::ValidPtr((uintptr_t)wgo))
                e.weapon = Str(il::Invoke(A.mObjGetName, wgo, nullptr));

            F.ents.push_back(std::move(e));
        }

        if (!F.camera) {
            void* c = il::Invoke(A.mCamMain, nullptr, nullptr);
            if (il::ValidPtr((uintptr_t)c)) F.camera = (uintptr_t)c;
        }

        for (uintptr_t b : il::FindObjectsOfType(A.kBomb))
            if (il::Read<uint8_t>(b + Cfg::Bomb_IsActive)) { F.bomb = b; break; }
    }

    float Median(std::vector<float>& v)
    {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    }

    V3 MedianPoint(const std::vector<V3>& pts)
    {
        std::vector<float> xs, ys, zs;
        for (const V3& p : pts) { xs.push_back(p.x); ys.push_back(p.y); zs.push_back(p.z); }
        return { Median(xs), Median(ys), Median(zs) };
    }

    float Dist(const V3& a, const V3& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    void CollectBones(Ent& e)
    {
        e.bones.clear();
        e.reach = 0.f;
        if (!A.mGetBones) return;
        uintptr_t arr = (uintptr_t)il::Invoke(A.mGetBones, (void*)e.body, nullptr);
        if (!il::ValidPtr(arr)) return;
        uint64_t len = il::Read<uint64_t>(arr + 0x18);           // Il2CppArray: max_length @0x18, data @0x20
        if (len < 6 || len > Cfg::MaxSourceBones) return;

        std::vector<uintptr_t> tr;
        std::vector<V3> pos;
        for (uint64_t i = 0; i < len; i++) {
            uintptr_t t = il::Read<uintptr_t>(arr + 0x20 + i * 8);
            if (!il::ValidPtr(t)) continue;
            V3 p = il::InvokeBoxed<V3>(A.mGetPosition, (void*)t, nullptr);
            if (!Finite(p)) continue;
            tr.push_back(t); pos.push_back(p);
        }
        if (pos.size() < 6) return;

        V3 c = MedianPoint(pos);
        std::vector<float> d;
        for (const V3& p : pos) d.push_back(Dist(p, c));
        std::vector<float> dm = d;
        float lim = std::max(Median(dm) * Cfg::OutlierK, 1e-3f);

        std::vector<size_t> keep;
        for (size_t i = 0; i < pos.size(); i++) if (d[i] <= lim) keep.push_back(i);
        if (keep.size() < 6) return;

        std::vector<size_t> pick;
        auto addBest = [&](auto less, size_t n) {
            std::vector<size_t> idx = keep;
            size_t k = std::min(n, idx.size());
            std::partial_sort(idx.begin(), idx.begin() + k, idx.end(), less);
            pick.insert(pick.end(), idx.begin(), idx.begin() + k);
        };
        auto hd = [&](size_t i) { float dx = pos[i].x - c.x, dz = pos[i].z - c.z; return dx * dx + dz * dz; };

        addBest([&](size_t a, size_t b) { return pos[a].y < pos[b].y; }, Cfg::PickLow);    // ступни
        addBest([&](size_t a, size_t b) { return pos[a].y > pos[b].y; }, Cfg::PickHigh);   // голова
        addBest([&](size_t a, size_t b) { return hd(a) > hd(b); }, Cfg::PickSide);         // руки, разброс по бокам
        for (size_t i = 0; i < keep.size(); i += Cfg::PickStride) pick.push_back(keep[i]);

        std::sort(pick.begin(), pick.end());
        pick.erase(std::unique(pick.begin(), pick.end()), pick.end());

        float maxD = 0.f;
        for (size_t i : pick) { e.bones.push_back(tr[i]); maxD = std::max(maxD, d[i]); }

        // Кость головы гарантированно в наборе.
        if (A.mGetHeadBone) {
            uintptr_t hb = (uintptr_t)il::Invoke(A.mGetHeadBone, (void*)e.skeleton, nullptr);
            if (il::ValidPtr(hb) && std::find(e.bones.begin(), e.bones.end(), hb) == e.bones.end())
                e.bones.push_back(hb);
        }
        e.reach = maxD * Cfg::ReachK;
    }

    // Мировой AABB по костям. false, если костей мало.
        // Мировой AABB по отслеживаемым костям. false, если костей мало.
    bool BoneAABB(const Ent& e, V3& mn, V3& mx)
    {
        std::vector<V3> pts;
        pts.reserve(e.bones.size());
        for (uintptr_t b : e.bones) {
            V3 p = il::InvokeBoxed<V3>(A.mGetPosition, (void*)b, nullptr);
            if (Finite(p)) pts.push_back(p);
        }
        if (pts.size() < 4) return false;

        V3 c = MedianPoint(pts);
        bool any = false;
        for (const V3& p : pts) {
            if (e.reach > 0.f && Dist(p, c) > e.reach) continue;
            if (!any) { mn = mx = p; any = true; continue; }
            mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
            mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
        }
        if (!any) return false;

        float h = mx.y - mn.y;
        if (!(h > 1e-3f)) return false;
        mn.y -= h * Cfg::PadBottom; mx.y += h * Cfg::PadTop;
        mn.x -= h * Cfg::PadSide;   mx.x += h * Cfg::PadSide;
        mn.z -= h * Cfg::PadSide;   mx.z += h * Cfg::PadSide;
        return true;
    }

    bool W2S(const V3& w, float sx, float sy, float screenH, ImVec2& out)
    {
        V3 p = w;
        void* a[1] = { &p };
        V3 s = il::InvokeBoxed<V3>(A.mW2S, (void*)F.camera, a);
        if (!Finite(s) || s.z <= 0.f) return false;
        out = ImVec2(s.x * sx, (screenH - s.y) * sy);
        return true;
    }

    ImU32 Col(const float c[4]) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])); }

    void Text(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* t, bool centered)
    {
        ImVec2 sz = ImGui::CalcTextSize(t);
        if (centered) pos.x -= sz.x * 0.5f;
        dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), t);
        dl->AddText(pos, col, t);
    }

    void CornerBox(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col)
    {
        float lw = (b.x - a.x) * 0.25f, lh = (b.y - a.y) * 0.25f;
        dl->AddLine(a, ImVec2(a.x + lw, a.y), col);  dl->AddLine(a, ImVec2(a.x, a.y + lh), col);
        dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - lw, a.y), col);  dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + lh), col);
        dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + lw, b.y), col);  dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - lh), col);
        dl->AddLine(b, ImVec2(b.x - lw, b.y), col);  dl->AddLine(b, ImVec2(b.x, b.y - lh), col);
    }

    void StatusLine(ImDrawList* dl, bool ilReady, bool camOk)
    {
        if (!kStatus) return;
        char t[256];
        if (!ilReady)       std::snprintf(t, sizeof t, "ESP: IL2CPP not ready");
        else if (!A.ready)  std::snprintf(t, sizeof t, "ESP: missing %s", A.missing.c_str());
        else std::snprintf(t, sizeof t,
            "ESP pc=%d loc=%d noSk=%d noBd=%d bones=%d cam=%d | team=%d dead=%d bnd=%d beh=%d tiny=%d fb=%d draw=%d",
            S.pc, S.local, S.noSkel, S.noBody, S.withBones, camOk ? 1 : 0,
            S.teamSkipped, S.dead, S.boundsBad, S.behind, S.tiny, S.fallback, S.drawn);
        dl->AddText(ImVec2(11, 61), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(ImVec2(10, 60), IM_COL32(255, 255, 0, 255), t);
    }

    float g_bombMax = 0.f;
}

void visuals::run()
{
    bool ilReady = il::Init();
    if (!esp_enabled) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!ilReady || !ResolveApi()) { StatusLine(dl, ilReady, false); return; }

    double now = ImGui::GetTime();
    if (F.lastRefresh < 0.0 || now - F.lastRefresh >= Cfg::RefreshInterval) {
        F.lastRefresh = now;
        Refresh();
    }

    bool camOk = il::ValidPtr(F.camera);
    S.teamSkipped = S.dead = S.boundsBad = S.behind = S.tiny = S.drawn = S.fallback = 0;

    int sw = il::InvokeBoxed<int>(A.mScreenW, nullptr, nullptr);
    int sh = il::InvokeBoxed<int>(A.mScreenH, nullptr, nullptr);
    if (!camOk || sw <= 0 || sh <= 0) { StatusLine(dl, true, camOk); return; }

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    float sx = disp.x / (float)sw, sy = disp.y / (float)sh;

    for (const Ent& e : F.ents) {
        if (F.haveLocal && !F.localTeam.empty() && e.team == F.localTeam) { S.teamSkipped++; continue; }

        // Смерть: любой из признаков. Если геттер не отработал, признак не считается сработавшим.
        int hp = 0; bool hpOk = CallInt(A.mPcHealth, (void*)e.pc, hp);
        bool alive = true; bool aliveOk = CallBool(A.mPlayerIsAlive, (void*)e.player, alive);
        if (il::Read<uint8_t>(e.pc + Cfg::PC_IsDeath) || il::Read<uint8_t>(e.pc + Cfg::PC_IsRagdolling) ||
            (aliveOk && !alive) || (hpOk && hp <= 0)) { S.dead++; continue; }

        // Мировой AABB: по костям, иначе по bounds рендерера.
        V3 mn{}, mx{};
        if (!BoneAABB(e, mn, mx)) {
            Bounds bb = il::InvokeBoxed<Bounds>(A.mGetBounds, (void*)e.body, nullptr);
            if (!Finite(bb.center) || !Finite(bb.extents)) { S.boundsBad++; continue; }
            mn = { bb.center.x - bb.extents.x, bb.center.y - bb.extents.y, bb.center.z - bb.extents.z };
            mx = { bb.center.x + bb.extents.x, bb.center.y + bb.extents.y, bb.center.z + bb.extents.z };
            S.fallback++;
        }

        // 8 углов в экран; если хоть один за камерой, пропускаем.
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        bool okAll = true;
        for (int i = 0; i < 8 && okAll; i++) {
            V3 c{ (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
            ImVec2 s;
            if (!W2S(c, sx, sy, (float)sh, s)) { okAll = false; break; }
            minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
            minY = std::min(minY, s.y); maxY = std::max(maxY, s.y);
        }
        if (!okAll) { S.behind++; continue; }

        float h = maxY - minY;
        if (h < 4.f || h > disp.y * 3.f) { S.tiny++; continue; }
        ImVec2 a(minX, minY), b(maxX, maxY);
        ImVec2 sf((minX + maxX) * 0.5f, maxY);   // низ-центр бокса
        S.drawn++;

        if (boxFill) dl->AddRectFilled(a, b, Col(box_fill_color));
        if (box2D) {
            if (current_box_type == 0) {
                dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(0, 0, 0, 200));
                dl->AddRect(a, b, Col(box_color));
            } else CornerBox(dl, a, b, Col(box_color));
        }

        if (healthBar || healthBarText) {
            float frac = std::clamp((float)hp / Cfg::MaxHealth, 0.f, 1.f);
            if (healthBar) {
                dl->AddRectFilled(ImVec2(a.x - 6, a.y - 1), ImVec2(a.x - 3, b.y + 1), IM_COL32(0, 0, 0, 180));
                dl->AddRectFilled(ImVec2(a.x - 5, b.y - h * frac), ImVec2(a.x - 4, b.y), Col(healthbar_color));
            }
            if (healthBarText) {
                char t[16]; std::snprintf(t, sizeof t, "%d", hp);
                Text(dl, ImVec2(a.x - 6 - ImGui::CalcTextSize(t).x, b.y - h * frac), IM_COL32(255, 255, 255, 255), t, false);
            }
        }

        float topY = a.y - ImGui::GetTextLineHeight() - 2.f;
        if (names && !e.name.empty()) Text(dl, ImVec2(sf.x, topY), Col(names_color), e.name.c_str(), true);

        float botY = b.y + 2.f;
        if (weaponText && !e.weapon.empty()) {
            Text(dl, ImVec2(sf.x, botY), Col(weapon_text_color), e.weapon.c_str(), true);
            botY += ImGui::GetTextLineHeight();
        }
        if (distance && F.haveLocal) {
            V3 p = WorldPos((void*)e.pc);
            float d = std::sqrt((p.x - F.localPos.x) * (p.x - F.localPos.x) +
                                (p.y - F.localPos.y) * (p.y - F.localPos.y) +
                                (p.z - F.localPos.z) * (p.z - F.localPos.z));
            char t[24]; std::snprintf(t, sizeof t, "%.0f m", d);
            Text(dl, ImVec2(sf.x, botY), Col(distance_color), t, true);
        }
        if (tracer) dl->AddLine(ImVec2(disp.x * 0.5f, disp.y), sf, Col(tracer_color));
    }

    if (bomb_timer_bar && A.mBombTimeLeft && il::ValidPtr(F.bomb) && il::Read<uint8_t>(F.bomb + Cfg::Bomb_IsActive)) {
        float left = il::InvokeBoxed<float>(A.mBombTimeLeft, (void*)F.bomb, nullptr);
        if (std::isfinite(left) && left > 0.f) {
            g_bombMax = std::max(g_bombMax, left);
            float frac = std::clamp(left / g_bombMax, 0.f, 1.f);
            float bw = disp.x * 0.3f, x0 = (disp.x - bw) * 0.5f, y0 = 24.f;
            dl->AddRectFilled(ImVec2(x0 - 1, y0 - 1), ImVec2(x0 + bw + 1, y0 + 7), IM_COL32(0, 0, 0, 180));
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * frac, y0 + 6), Col(bomb_timer_color));
            char t[16]; std::snprintf(t, sizeof t, "%.1f", left);
            Text(dl, ImVec2(disp.x * 0.5f, y0 + 8), Col(bomb_timer_color), t, true);
        }
    } else g_bombMax = 0.f;

    StatusLine(dl, true, camOk);
}