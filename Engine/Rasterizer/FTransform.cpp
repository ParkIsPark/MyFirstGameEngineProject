#include "FTransform.h"
#include "ACamera.h"

// world->eye: basis vectors as rows; translation = -dot(axis, eye).
//   eye = ( u.(p-e), v.(p-e), w.(p-e) ).  Stored column-major (V[col][row]).
glm::mat4 FTransform::MakeView(const ACamera& cam)
{
    const glm::vec3& u = cam.u;
    const glm::vec3& v = cam.v;
    const glm::vec3& w = cam.w;
    const glm::vec3& e = cam.eye;

    glm::mat4 V(1.0f);
    V[0][0] = u.x; V[1][0] = u.y; V[2][0] = u.z; V[3][0] = -glm::dot(u, e);
    V[0][1] = v.x; V[1][1] = v.y; V[2][1] = v.z; V[3][1] = -glm::dot(v, e);
    V[0][2] = w.x; V[1][2] = w.y; V[2][2] = w.z; V[3][2] = -glm::dot(w, e);
    return V;
}

// FCG perspective (Shirley). Bottom row [0 0 1 0] -> clip.w = z_eye.
//   [ 2n/(r-l)   0          (l+r)/(l-r)   0          ]
//   [ 0          2n/(t-b)   (b+t)/(b-t)   0          ]
//   [ 0          0          (n+f)/(n-f)   -2nf/(n-f) ]
//   [ 0          0          1             0          ]
glm::mat4 FTransform::MakeProjFCG(float l, float r, float b, float t, float n, float f)
{
    glm::mat4 P(0.0f);
    P[0][0] = 2.0f * n / (r - l);
    P[1][1] = 2.0f * n / (t - b);
    P[2][0] = (l + r) / (l - r);
    P[2][1] = (b + t) / (b - t);
    P[2][2] = (n + f) / (n - f);
    P[2][3] = 1.0f;                       // bottom row [0 0 1 0]
    P[3][2] = -2.0f * n * f / (n - f);
    return P;
}

// NDC -> screen: x = (ndc.x+1)*nx/2, y = (ndc.y+1)*ny/2, z = (ndc.z+1)*0.5.
glm::mat4 FTransform::MakeViewport(int nx, int ny)
{
    glm::mat4 VP(1.0f);
    VP[0][0] = nx * 0.5f;
    VP[1][1] = ny * 0.5f;
    VP[2][2] = 0.5f;
    VP[3][0] = nx * 0.5f;
    VP[3][1] = ny * 0.5f;
    VP[3][2] = 0.5f;
    return VP;
}
