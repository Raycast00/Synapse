#include "visuals.h"
#include "il2cpp/il2cpp.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

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
        constexpr float  MaxHealth       = 100.f;

        constexpr size_t MaxSourceBones = 512;
        constexpr size_t PickLow = 3, PickHigh = 3, PickSide = 6, PickStride = 8;
        constexpr float  PadTop    = 0.08f;
        constexpr float  PadBottom = 0.10f;
        constexpr float  PadSide   = 0.05f;
        constexpr float  OutlierK  = 3.5f;   // при выборе: кость дальше K * медианной дистанции отбрасывается
        constexpr float  ReachK    = 1.6f;

        // Траектория гранат (не из дампа, подбирать в игре).
        constexpr float  TrailMaxAgeSec = 2.4f;   // с запасом сверх подтверждённого GrenadeManager.MaxThrowAge = 2
        constexpr float  TrailPointGap  = 0.05f;  // новая сырая точка не чаще, чем раз на столько мировых юнитов
        constexpr float  SmoothSegLen   = 0.12f;  // целевая длина одного кусочка сглаженной кривой

        // Верхний ряд пилюль (Health Text / Names / Distance) — стиль не из дампа, подбирать на глаз.
        constexpr float PillPadX     = 7.f;
        constexpr float PillPadY     = 3.f;
        constexpr float PillGap      = 4.f;
        constexpr float PillRounding = 4.f;
    }

    struct Api
    {
        bool ready = false;
        std::string missing;
        void *kPlayerController{}, *kPlayer{}, *kSkeleton{}, *kBomb{}, *kSMR{};
        void *kGrenade{}, *kPhysics{};
        void *mGetTransform{}, *mGetPosition{}, *mW2S{}, *mCamMain{};
        void *mGetBounds{}, *mGetCompInChildren{}, *mObjGetName{}, *mGetBones{}, *mGetHeadBone{}, *mGetParent{};
        void *mScreenW{}, *mScreenH{};
        void *mPcHealth{}, *mPlayerTeam{}, *mPlayerIsAlive{}, *mSkelWeapon{}, *mBombTimeLeft{};
        void *mGrenPos{}, *mGrenVel{}, *mGrenSpawn{}, *mGrenVisible{}, *mGrenResting{}, *mGravity{};
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
        A.kGrenade          = il::Class("ScarletWork.Game.Grenades", "GrenadeProjectile", "Assembly-CSharp");

        void* kComponent = il::Class("UnityEngine", "Component", core);
        void* kTransform = il::Class("UnityEngine", "Transform", core);
        void* kCamera    = il::Class("UnityEngine", "Camera", core);
        void* kRenderer  = il::Class("UnityEngine", "Renderer", core);
        void* kScreen    = il::Class("UnityEngine", "Screen", core);
        void* kObject    = il::Class("UnityEngine", "Object", core);
        A.kSMR           = il::Class("UnityEngine", "SkinnedMeshRenderer", core);
        A.kPhysics       = il::Class("UnityEngine", "Physics", core);

        A.mGetTransform      = il::Method(kComponent, "get_transform", 0);
        A.mGetCompInChildren = il::Method(kComponent, "GetComponentInChildren", 2);
        A.mGetPosition       = il::Method(kTransform, "get_position", 0);
        A.mGetParent         = il::Method(kTransform, "get_parent", 0);    // опционально: без него скелет не рисуется
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

        A.mGrenPos      = il::Method(A.kGrenade, "get_Position", 0);        // опционально, для траектории гранат
        A.mGrenVel      = il::Method(A.kGrenade, "get_SyncVelocity", 0);    // опционально
        A.mGrenSpawn    = il::Method(A.kGrenade, "get_SpawnTime", 0);       // опционально
        A.mGrenVisible  = il::Method(A.kGrenade, "get_IsVisible", 0);       // опционально
        A.mGrenResting  = il::Method(A.kGrenade, "get_IsResting", 0);       // опционально
        A.mGravity      = il::Method(A.kPhysics, "get_gravity", 0);        // опционально

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
        std::vector<uintptr_t> bones;   // Transform* костей скина (подвыборка для бокса)
        std::vector<std::pair<uintptr_t, int>> skelBones;   // {Transform*, индекс родителя в этом же векторе, или -1}
        float reach = 0.f;
        std::string name, team, weapon;
    };

    struct Stats
    {
        int pc = 0, local = 0, noPlayer = 0, noSkel = 0, noBody = 0, withBones = 0, withSkel = 0;
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

    // Полный скелет для линий: все кости SkinnedMeshRenderer с их реальными родителями внутри этого же
    // набора (Transform.get_parent()). Никаких имён вроде "рука"/"нога" — ровно то, что отдаёт модель.
    void CollectSkeletonBones(Ent& e)
    {
        e.skelBones.clear();
        if (!A.mGetParent || !A.mGetBones || !il::ValidPtr(e.body)) return;

        uintptr_t arr = (uintptr_t)il::Invoke(A.mGetBones, (void*)e.body, nullptr);
        if (!il::ValidPtr(arr)) return;
        uint64_t len = il::Read<uint64_t>(arr + 0x18);
        if (len < 2 || len > Cfg::MaxSourceBones) return;

        std::vector<uintptr_t> tr;
        std::vector<V3> pos;
        for (uint64_t i = 0; i < len; i++) {
            uintptr_t t = il::Read<uintptr_t>(arr + 0x20 + i * 8);
            if (!il::ValidPtr(t)) continue;
            V3 p = il::InvokeBoxed<V3>(A.mGetPosition, (void*)t, nullptr);
            if (!Finite(p)) continue;
            tr.push_back(t); pos.push_back(p);
        }
        if (tr.size() < 2) return;

        V3 c = MedianPoint(pos);
        std::vector<float> d;
        for (const V3& p : pos) d.push_back(Dist(p, c));
        std::vector<float> dm = d;
        float lim = std::max(Median(dm) * Cfg::OutlierK, 1e-3f);

        std::vector<uintptr_t> chosen;
        for (size_t i = 0; i < tr.size(); i++) if (d[i] <= lim) chosen.push_back(tr[i]);
        if (chosen.size() < 2) return;

        for (size_t idx = 0; idx < chosen.size(); idx++) {
            void* parent = il::Invoke(A.mGetParent, (void*)chosen[idx], nullptr);
            int parentIdx = -1;
            if (il::ValidPtr((uintptr_t)parent)) {
                for (size_t j = 0; j < chosen.size(); j++) {
                    if (chosen[j] == (uintptr_t)parent) { parentIdx = (int)j; break; }
                }
            }
            e.skelBones.push_back({ chosen[idx], parentIdx });
        }
    }

    void Refresh()
    {
        F.ents.clear();
        F.camera = 0; F.haveLocal = false; F.localTeam.clear(); F.bomb = 0;
        S.pc = S.local = S.noPlayer = S.noSkel = S.noBody = S.withBones = S.withSkel = 0;

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

            if (visuals::skeleton) CollectSkeletonBones(e);
            if (!e.skelBones.empty()) S.withSkel++;

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

    // ---- Верхний ряд пилюль: Health Text (слева) — Names (центр) — Distance (справа). ----
    // Весь ряд центрируется над боксом как единая группа: если элемента нет, оставшиеся не "съезжают"
    // на его место, а пересчитывают общий центр заново.
    struct PillItem { std::string text; ImU32 textCol; };

    void DrawPillRow(ImDrawList* dl, float centerX, float y, const std::vector<PillItem>& items)
    {
        if (items.empty()) return;
        const ImU32 bg     = IM_COL32(26, 16, 14, 215);
        const ImU32 border = IM_COL32(95, 62, 54, 255);

        std::vector<ImVec2> sizes(items.size());
        float totalW = 0.f, rowH = 0.f;
        for (size_t i = 0; i < items.size(); i++) {
            ImVec2 ts = ImGui::CalcTextSize(items[i].text.c_str());
            sizes[i] = ImVec2(ts.x + Cfg::PillPadX * 2.f, ts.y + Cfg::PillPadY * 2.f);
            rowH = std::max(rowH, sizes[i].y);
            totalW += sizes[i].x;
            if (i + 1 < items.size()) totalW += Cfg::PillGap;
        }

        float x = centerX - totalW * 0.5f;
        for (size_t i = 0; i < items.size(); i++) {
            ImVec2 tl(x, y), br(x + sizes[i].x, y + rowH);
            dl->AddRectFilled(tl, br, bg, Cfg::PillRounding);
            dl->AddRect(tl, br, border, Cfg::PillRounding, 0, 1.0f);

            ImVec2 ts = ImGui::CalcTextSize(items[i].text.c_str());
            ImVec2 tp(tl.x + (sizes[i].x - ts.x) * 0.5f, tl.y + (rowH - ts.y) * 0.5f);
            dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 220), items[i].text.c_str());
            dl->AddText(tp, items[i].textCol, items[i].text.c_str());

            x += sizes[i].x + Cfg::PillGap;
        }
    }

    void StatusLine(ImDrawList* dl, bool ilReady, bool camOk)
    {
        if (!kStatus) return;
        char t[256];
        if (!ilReady)       std::snprintf(t, sizeof t, "ESP: IL2CPP not ready");
        else if (!A.ready)  std::snprintf(t, sizeof t, "ESP: missing %s", A.missing.c_str());
        else std::snprintf(t, sizeof t,
            "ESP pc=%d loc=%d noSk=%d noBd=%d bones=%d skel=%d cam=%d | team=%d dead=%d bnd=%d beh=%d tiny=%d fb=%d draw=%d",
            S.pc, S.local, S.noSkel, S.noBody, S.withBones, S.withSkel, camOk ? 1 : 0,
            S.teamSkipped, S.dead, S.boundsBad, S.behind, S.tiny, S.fallback, S.drawn);
        dl->AddText(ImVec2(11, 61), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(ImVec2(10, 60), IM_COL32(255, 255, 0, 255), t);
    }

    float g_bombMax = 0.f;

    // ---- Траектория гранат ----
    struct GrenTrail
    {
        std::vector<V3>    raw;     // сырые реальные позиции, хронологически
        std::vector<float> rawAge;  // время жизни каждой сырой точки на момент записи (ImGui::GetTime())
        std::vector<V3>    smooth;  // сглаженные подточки между сырыми (кэш, не пересчитывается заново)
        double             spawnTime = -1.0;
    };
    std::unordered_map<uintptr_t, GrenTrail> g_grenTrails;

    V3 CatmullRom(const V3& p0, const V3& p1, const V3& p2, const V3& p3, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        auto comp = [&](float a0, float a1, float a2, float a3) {
            return 0.5f * ((2.f * a1) +
                           (-a0 + a2) * t +
                           (2.f * a0 - 5.f * a1 + 4.f * a2 - a3) * t2 +
                           (-a0 + 3.f * a1 - 3.f * a2 + a3) * t3);
        };
        return { comp(p0.x, p1.x, p2.x, p3.x), comp(p0.y, p1.y, p2.y, p3.y), comp(p0.z, p1.z, p2.z, p3.z) };
    }

    // Как только пришла новая сырая точка R[n], достраиваем кривую для ПРЕДЫДУЩЕГО сегмента R[n-2]→R[n-1],
    // используя соседей для касательных. Самый свежий сегмент (к текущей позиции гранаты) остаётся прямым —
    // он "дорисовывается" каждый кадр отдельно в DrawGrenadeTrajectories, пока не придёт следующая сырая точка.
    void AppendSmoothSegment(GrenTrail& tr)
    {
        size_t n = tr.raw.size() - 1;
        if (n < 2) return;
        const V3& p3 = tr.raw[n];
        const V3& p2 = tr.raw[n - 1];
        const V3& p1 = tr.raw[n - 2];
        const V3& p0 = (n >= 3) ? tr.raw[n - 3] : tr.raw[0];

        float segLen = Dist(p1, p2);
        int steps = std::clamp((int)std::ceil(segLen / Cfg::SmoothSegLen), 2, 10);
        for (int i = 1; i <= steps; i++) {
            float t = (float)i / steps;
            tr.smooth.push_back(CatmullRom(p0, p1, p2, p3, t));
        }
    }

    void DrawGrenadeTrajectories(ImDrawList* dl, float sx, float sy, int sh)
    {
        if (!A.mGrenPos) return;   // класс/метод не нашёлся — тихо выходим, остальной ESP не трогаем

        std::vector<uintptr_t> live = il::FindObjectsOfType(A.kGrenade);
        std::vector<uintptr_t> seen;
        seen.reserve(live.size());
        double now = ImGui::GetTime();

        for (uintptr_t g : live) {
            if (!il::ValidPtr(g)) continue;
            bool visible = true;
            CallBool(A.mGrenVisible, (void*)g, visible);     // нет геттера — считаем видимой
            if (!visible) continue;

            double spawn = -1.0;
            if (A.mGrenSpawn) {
                void* r = il::Invoke(A.mGrenSpawn, (void*)g, nullptr);
                if (il::ValidPtr((uintptr_t)r)) spawn = *(double*)((uint8_t*)r + 0x10);
            }

            V3 pos = il::InvokeBoxed<V3>(A.mGrenPos, (void*)g, nullptr);
            if (!Finite(pos)) continue;

            seen.push_back(g);
            GrenTrail& tr = g_grenTrails[g];
            if (spawn >= 0.0 && tr.spawnTime >= 0.0 && std::fabs(spawn - tr.spawnTime) > 0.001) {
                tr.raw.clear(); tr.rawAge.clear(); tr.smooth.clear();   // новый бросок в том же объекте из пула
            }
            tr.spawnTime = spawn;

            if (tr.raw.empty() || Dist(tr.raw.back(), pos) > Cfg::TrailPointGap) {
                tr.raw.push_back(pos);
                tr.rawAge.push_back((float)now);
                AppendSmoothSegment(tr);
            }

            // Обрезаем по возрасту (ориентир — подтверждённый GrenadeManager.MaxThrowAge = 2 c, с запасом).
            while (tr.rawAge.size() > 1 && (float)now - tr.rawAge.front() > Cfg::TrailMaxAgeSec) {
                tr.raw.erase(tr.raw.begin());
                tr.rawAge.erase(tr.rawAge.begin());
            }
            if (tr.smooth.size() > 2000) tr.smooth.erase(tr.smooth.begin(), tr.smooth.begin() + (tr.smooth.size() - 2000));

            // Сглаженная часть следа — с градиентом прозрачности от старой (тусклой) к свежей (яркой).
            size_t total = tr.smooth.size();
            for (size_t i = 1; i < total; i++) {
                ImVec2 a, b;
                if (!W2S(tr.smooth[i - 1], sx, sy, (float)sh, a)) continue;
                if (!W2S(tr.smooth[i], sx, sy, (float)sh, b)) continue;
                float k = (total > 1) ? (float)i / (float)(total - 1) : 1.f;
                float alphaMul = 0.15f + 0.85f * k;
                ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
                    visuals::grenade_trajectory_color[0], visuals::grenade_trajectory_color[1],
                    visuals::grenade_trajectory_color[2], visuals::grenade_trajectory_color[3] * alphaMul));
                dl->AddLine(a, b, col, 2.2f);
            }
            // Самый свежий, ещё не сглаженный хвост — прямой отрезок до текущей позиции гранаты.
            if (tr.raw.size() >= 2) {
                ImVec2 a, b;
                if (W2S(tr.raw[tr.raw.size() - 2], sx, sy, (float)sh, a) && W2S(pos, sx, sy, (float)sh, b))
                    dl->AddLine(a, b, Col(visuals::grenade_trajectory_color), 2.2f);
            }

            // Короткий честный прогноз вперёд: v*t + 0.5*g*t^2, без отскоков от стен.
            bool resting = false;
            CallBool(A.mGrenResting, (void*)g, resting);
            if (!resting && A.mGrenVel && A.mGravity) {
                V3 vel = il::InvokeBoxed<V3>(A.mGrenVel, (void*)g, nullptr);
                V3 gra = il::InvokeBoxed<V3>(A.mGravity, nullptr, nullptr);
                if (Finite(vel) && Finite(gra)) {
                    ImU32 predCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
                        visuals::grenade_trajectory_color[0], visuals::grenade_trajectory_color[1],
                        visuals::grenade_trajectory_color[2], visuals::grenade_trajectory_color[3] * 0.45f));
                    constexpr int   kSteps  = 36;
                    constexpr float kTotalT = 0.5f;
                    V3 prev = pos;
                    for (int i = 1; i <= kSteps; i++) {
                        float t = kTotalT * (float)i / kSteps;
                        V3 p{ pos.x + vel.x * t + 0.5f * gra.x * t * t,
                              pos.y + vel.y * t + 0.5f * gra.y * t * t,
                              pos.z + vel.z * t + 0.5f * gra.z * t * t };
                        if (!Finite(p)) break;
                        ImVec2 a, b;
                        bool okA = W2S(prev, sx, sy, (float)sh, a);
                        bool okB = W2S(p, sx, sy, (float)sh, b);
                        if (okA && okB) {
                            dl->AddLine(a, b, predCol, 2.0f);
                            if (i % 6 == 0) dl->AddCircleFilled(b, 2.5f, predCol, 8);   // точки-маркеры вдоль дуги
                        }
                        prev = p;
                    }
                }
            }
        }

        for (auto it = g_grenTrails.begin(); it != g_grenTrails.end(); ) {
            if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) it = g_grenTrails.erase(it);
            else ++it;
        }
    }
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

        if (skeleton) {
            ImU32 skelCol = Col(skeleton_color);
            for (const auto& bone : e.skelBones) {
                uintptr_t childTr = bone.first;
                int parentIdx = bone.second;
                if (parentIdx < 0) continue;
                uintptr_t parentTr = e.skelBones[parentIdx].first;
                V3 cp = il::InvokeBoxed<V3>(A.mGetPosition, (void*)childTr, nullptr);
                V3 pp = il::InvokeBoxed<V3>(A.mGetPosition, (void*)parentTr, nullptr);
                if (!Finite(cp) || !Finite(pp)) continue;
                ImVec2 cs, ps;
                if (!W2S(cp, sx, sy, (float)sh, cs)) continue;
                if (!W2S(pp, sx, sy, (float)sh, ps)) continue;
                dl->AddLine(cs, ps, skelCol);
            }
        }

        // Вертикальная полоска здоровья сбоку от бокса — отдельно от верхнего ряда пилюль.
        if (healthBar) {
            float frac = std::clamp((float)hp / Cfg::MaxHealth, 0.f, 1.f);
            dl->AddRectFilled(ImVec2(a.x - 6, a.y - 1), ImVec2(a.x - 3, b.y + 1), IM_COL32(0, 0, 0, 180));
            dl->AddRectFilled(ImVec2(a.x - 5, b.y - h * frac), ImVec2(a.x - 4, b.y), Col(healthbar_color));
        }

        // Верхний ряд пилюль: Health Text (слева) — Names (центр) — Distance (справа).
        // Ряд центрируется как единая группа: если элемента нет, остальные пересчитывают центр заново.
        std::vector<PillItem> row;
        if (healthBarText) {
            char t[16]; std::snprintf(t, sizeof t, "%d", hp);
            row.push_back({ t, IM_COL32(255, 255, 255, 255) });
        }
        if (names && !e.name.empty())
            row.push_back({ e.name, Col(names_color) });
        if (distance && F.haveLocal) {
            V3 p = WorldPos((void*)e.pc);
            float d = std::sqrt((p.x - F.localPos.x) * (p.x - F.localPos.x) +
                                (p.y - F.localPos.y) * (p.y - F.localPos.y) +
                                (p.z - F.localPos.z) * (p.z - F.localPos.z));
            char t[24]; std::snprintf(t, sizeof t, "%.0fm", d);
            row.push_back({ t, Col(distance_color) });
        }
        if (!row.empty()) {
            float rowH = ImGui::GetTextLineHeight() + Cfg::PillPadY * 2.f;
            float topY = a.y - rowH - 3.f;
            DrawPillRow(dl, sf.x, topY, row);
        }

        float botY = b.y + 2.f;
        if (weaponText && !e.weapon.empty()) {
            Text(dl, ImVec2(sf.x, botY), Col(weapon_text_color), e.weapon.c_str(), true);
            botY += ImGui::GetTextLineHeight();
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

    if (visualize_grenade_trajectory) DrawGrenadeTrajectories(dl, sx, sy, sh);

    StatusLine(dl, true, camOk);
}