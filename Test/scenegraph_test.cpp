// ---------------------------------------------------------------------------
// scenegraph_test.cpp — GL-free self-test for USceneComponent (P1).
//
// Verifies the scene-graph transform math, the dirty-flag world-matrix cache,
// and attach/detach. Each gate prints "[T#] ... -> PASS|FAIL".
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/World \
//       Test/scenegraph_test.cpp Engine/World/USceneComponent.cpp \
//       -o scenegraph_test && ./scenegraph_test
// ---------------------------------------------------------------------------
#include "USceneComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static bool veq(const glm::vec3& a, const glm::vec3& b, float e = 1e-4f)
{
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static bool meq(const glm::mat4& a, const glm::mat4& b, float e = 1e-4f)
{
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (std::fabs(a[c][r] - b[c][r]) > e) return false;
    return true;
}

int main()
{
    // T1: identity transform -> identity world
    {
        USceneComponent c;
        ck("T1", "default transform world == identity", meq(c.GetWorldMatrix(), glm::mat4(1.0f)));
    }
    // T2: no parent -> world == relative
    {
        USceneComponent c;
        c.SetRelativeLocation(glm::vec3(3, -2, 5));
        ck("T2", "no parent: world == relative", meq(c.GetWorldMatrix(), c.GetRelativeMatrix()));
    }
    // T3: parent T=(10,0,0), child rel=(1,0,0) -> child world loc (11,0,0)
    {
        USceneComponent parent, child;
        parent.SetRelativeLocation(glm::vec3(10, 0, 0));
        child.SetRelativeLocation(glm::vec3(1, 0, 0));
        child.AttachTo(&parent);
        ck("T3", "child world loc == parent*rel (11,0,0)", veq(child.GetWorldLocation(), glm::vec3(11, 0, 0)));
    }
    // T4: move parent -> child world updates automatically
    {
        USceneComponent parent, child;
        child.SetRelativeLocation(glm::vec3(1, 0, 0));
        child.AttachTo(&parent);
        glm::vec3 before = child.GetWorldLocation();             // (1,0,0)
        parent.SetRelativeLocation(glm::vec3(0, 5, 0));
        glm::vec3 after = child.GetWorldLocation();              // (1,5,0)
        ck("T4", "parent move propagates to child", veq(before, glm::vec3(1, 0, 0)) && veq(after, glm::vec3(1, 5, 0)));
    }
    // T5: dirty propagation -- changing parent invalidates child (covered via T4 result)
    {
        USceneComponent parent, child;
        child.AttachTo(&parent);
        child.GetWorldMatrix();                                  // prime caches
        parent.SetRelativeScale(glm::vec3(2, 2, 2));
        bool ok = veq(child.GetWorldLocation(), glm::vec3(0)) && // still origin
                  meq(child.GetWorldMatrix(), parent.GetWorldMatrix()); // child rel identity -> equals parent
        ck("T5", "MarkDirty propagates parent->child", ok);
    }
    // T6: cache -- no recompute when nothing changed
    {
        USceneComponent c;
        c.SetRelativeLocation(glm::vec3(1, 2, 3));
        c.GetWorldMatrix();                                      // compute + cache
        long before = USceneComponent::s_recomputeCount;
        c.GetWorldMatrix(); c.GetWorldMatrix(); c.GetWorldLocation();
        long delta = USceneComponent::s_recomputeCount - before;
        ck("T6", "cached: no recompute when unchanged (delta==0)", delta == 0);
    }
    // T7: 3-level chain composition
    {
        USceneComponent a, b, c;
        a.SetRelativeLocation(glm::vec3(1, 0, 0));
        b.SetRelativeLocation(glm::vec3(0, 2, 0));
        c.SetRelativeLocation(glm::vec3(0, 0, 3));
        b.AttachTo(&a); c.AttachTo(&b);
        glm::mat4 expect = a.GetRelativeMatrix() * b.GetRelativeMatrix() * c.GetRelativeMatrix();
        ck("T7", "3-level world == Ma*Mb*Mc", meq(c.GetWorldMatrix(), expect) && veq(c.GetWorldLocation(), glm::vec3(1, 2, 3)));
    }
    // T8: AttachTo registers child + sets parent
    {
        USceneComponent parent, child;
        child.AttachTo(&parent);
        bool ok = child.attachParent == &parent &&
                  parent.children.size() == 1 && parent.children[0] == &child;
        ck("T8", "AttachTo wires attachParent + children", ok);
    }
    // T9: Detach clears the relationship
    {
        USceneComponent parent, child;
        child.AttachTo(&parent);
        child.Detach();
        ck("T9", "Detach clears parent + child list", child.attachParent == nullptr && parent.children.empty());
    }
    // T10: SetRelativeLocation marks dirty -> world reflects new value on read
    {
        USceneComponent c;
        c.GetWorldMatrix();                                      // cache identity
        c.SetRelativeLocation(glm::vec3(7, 8, 9));
        ck("T10", "SetRelativeLocation invalidates cache", veq(c.GetWorldLocation(), glm::vec3(7, 8, 9)));
    }

    std::printf("=== scenegraph: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
