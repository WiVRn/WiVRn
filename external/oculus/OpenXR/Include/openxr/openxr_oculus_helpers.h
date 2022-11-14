// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   openxr_oculus_helpers.h
Content     :   OpenXR Helper routines
Language    :   C99

*************************************************************************************/

#ifndef OPENXR_OCULUS_HELPERS_H
#define OPENXR_OCULUS_HELPERS_H 1

#include <openxr/openxr.h>

#include "math.h" // for cosf(), sinf(), tanf()

typedef enum {
    GRAPHICS_VULKAN,
    GRAPHICS_OPENGL,
    GRAPHICS_OPENGL_ES,
    GRAPHICS_D3D,
} GraphicsAPI;

// OpenXR time values are expressed in nanseconds.
static inline double FromXrTime(const XrTime time) {
    return (time * 1e-9);
}

static inline XrTime ToXrTime(const double timeInSeconds) {
    return (timeInSeconds * 1e9);
}

typedef struct XrMatrix4x4f {
    float m[16];
} XrMatrix4x4f;

static inline XrVector3f XrVector3f_Zero() {
    XrVector3f r;
    r.x = r.y = r.z = 0.0f;
    return r;
}

static inline XrVector3f XrVector3f_ScalarMultiply(const XrVector3f v, float scale) {
    XrVector3f u;
    u.x = v.x * scale;
    u.y = v.y * scale;
    u.z = v.z * scale;
    return u;
}

static inline float XrVector3f_Dot(const XrVector3f u, const XrVector3f v) {
    return u.x * v.x + u.y * v.y + u.z * v.z;
}

static inline XrVector3f XrVector3f_Add(const XrVector3f u, const XrVector3f v) {
    XrVector3f w;
    w.x = u.x + v.x;
    w.y = u.y + v.y;
    w.z = u.z + v.z;
    return w;
}

static inline float XrVector3f_LengthSquared(const XrVector3f v) {
    return XrVector3f_Dot(v, v);
}

static inline float XrVector3f_Length(const XrVector3f v) {
    return sqrtf(XrVector3f_LengthSquared(v));
}

static inline XrVector3f XrVector3f_Normalized(const XrVector3f v) {
    float rcpLen = 1.0f / XrVector3f_Length(v);
    return XrVector3f_ScalarMultiply(v, rcpLen);
}

static inline XrQuaternionf XrQuaternionf_Identity() {
    XrQuaternionf r;
    r.x = r.y = r.z = 0.0;
    r.w = 1.0f;
    return r;
}

static inline XrQuaternionf XrQuaternionf_CreateFromVectorAngle(
    const XrVector3f axis,
    const float angle) {
    XrQuaternionf r;
    if (XrVector3f_LengthSquared(axis) == 0.0f) {
        return XrQuaternionf_Identity();
    }

    XrVector3f unitAxis = XrVector3f_Normalized(axis);
    float sinHalfAngle = sinf(angle * 0.5f);

    r.w = cosf(angle * 0.5f);
    r.x = unitAxis.x * sinHalfAngle;
    r.y = unitAxis.y * sinHalfAngle;
    r.z = unitAxis.z * sinHalfAngle;
    return r;
}

static inline XrQuaternionf XrQuaternionf_Inverse(const XrQuaternionf q) {
    XrQuaternionf r;
    r.x = -q.x;
    r.y = -q.y;
    r.z = -q.z;
    r.w = q.w;
    return r;
}

static inline XrQuaternionf XrQuaternionf_Multiply(const XrQuaternionf a, const XrQuaternionf b) {
    XrQuaternionf c;
    c.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    c.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    c.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    c.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return c;
}

static inline XrVector3f XrQuaternionf_Rotate(const XrQuaternionf a, const XrVector3f v) {
    XrVector3f r;
    XrQuaternionf q = {v.x, v.y, v.z, 0.0f};
    XrQuaternionf aq = XrQuaternionf_Multiply(a, q);
    XrQuaternionf aInv = XrQuaternionf_Inverse(a);
    XrQuaternionf aqaInv = XrQuaternionf_Multiply(aq, aInv);
    r.x = aqaInv.x;
    r.y = aqaInv.y;
    r.z = aqaInv.z;
    return r;
}

static inline XrQuaternionf XrQuaternionf_Normalize(const XrQuaternionf q) {
    double n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n != 0.0f) {
        n = 1.0f / n;
    }
    XrQuaternionf c;
    c.x = q.x * n;
    c.y = q.y * n;
    c.z = q.z * n;
    c.w = q.w * n;

    return c;
}

static inline XrPosef XrPosef_Identity() {
    XrPosef r;
    r.orientation = XrQuaternionf_Identity();
    r.position = XrVector3f_Zero();
    return r;
}

static inline XrPosef XrPosef_Create(const XrQuaternionf orientation, const XrVector3f position) {
    XrPosef r;
    r.orientation = orientation;
    r.position = position;
    return r;
}

static inline XrVector3f XrPosef_Transform(const XrPosef a, const XrVector3f v) {
    XrVector3f r0 = XrQuaternionf_Rotate(a.orientation, v);
    return XrVector3f_Add(r0, a.position);
}

static inline XrPosef XrPosef_Multiply(const XrPosef a, const XrPosef b) {
    XrPosef c;
    c.orientation = XrQuaternionf_Multiply(a.orientation, b.orientation);
    c.position = XrPosef_Transform(a, b.position);
    return c;
}

static inline XrPosef XrPosef_Inverse(const XrPosef a) {
    XrPosef b;
    b.orientation = XrQuaternionf_Inverse(a.orientation);
    b.position = XrQuaternionf_Rotate(b.orientation, XrVector3f_ScalarMultiply(a.position, -1.0f));
    return b;
}

// Creates a scale matrix.
static inline void
XrMatrix4x4f_CreateScale(XrMatrix4x4f* result, const float x, const float y, const float z) {
    result->m[0] = x;
    result->m[1] = 0.0f;
    result->m[2] = 0.0f;
    result->m[3] = 0.0f;
    result->m[4] = 0.0f;
    result->m[5] = y;
    result->m[6] = 0.0f;
    result->m[7] = 0.0f;
    result->m[8] = 0.0f;
    result->m[9] = 0.0f;
    result->m[10] = z;
    result->m[11] = 0.0f;
    result->m[12] = 0.0f;
    result->m[13] = 0.0f;
    result->m[14] = 0.0f;
    result->m[15] = 1.0f;
}

// Creates a matrix from a quaternion.
static inline void XrMatrix4x4f_CreateFromQuaternion(
    XrMatrix4x4f* result,
    const XrQuaternionf* quat) {
    const float x2 = quat->x + quat->x;
    const float y2 = quat->y + quat->y;
    const float z2 = quat->z + quat->z;

    const float xx2 = quat->x * x2;
    const float yy2 = quat->y * y2;
    const float zz2 = quat->z * z2;

    const float yz2 = quat->y * z2;
    const float wx2 = quat->w * x2;
    const float xy2 = quat->x * y2;
    const float wz2 = quat->w * z2;
    const float xz2 = quat->x * z2;
    const float wy2 = quat->w * y2;

    result->m[0] = 1.0f - yy2 - zz2;
    result->m[1] = xy2 + wz2;
    result->m[2] = xz2 - wy2;
    result->m[3] = 0.0f;

    result->m[4] = xy2 - wz2;
    result->m[5] = 1.0f - xx2 - zz2;
    result->m[6] = yz2 + wx2;
    result->m[7] = 0.0f;

    result->m[8] = xz2 + wy2;
    result->m[9] = yz2 - wx2;
    result->m[10] = 1.0f - xx2 - yy2;
    result->m[11] = 0.0f;

    result->m[12] = 0.0f;
    result->m[13] = 0.0f;
    result->m[14] = 0.0f;
    result->m[15] = 1.0f;
}

// Creates an identity matrix.
static inline void XrMatrix4x4f_CreateIdentity(XrMatrix4x4f* result) {
    result->m[0] = 1.0f;
    result->m[1] = 0.0f;
    result->m[2] = 0.0f;
    result->m[3] = 0.0f;
    result->m[4] = 0.0f;
    result->m[5] = 1.0f;
    result->m[6] = 0.0f;
    result->m[7] = 0.0f;
    result->m[8] = 0.0f;
    result->m[9] = 0.0f;
    result->m[10] = 1.0f;
    result->m[11] = 0.0f;
    result->m[12] = 0.0f;
    result->m[13] = 0.0f;
    result->m[14] = 0.0f;
    result->m[15] = 1.0f;
}

// Creates a translation matrix.
static inline void
XrMatrix4x4f_CreateTranslation(XrMatrix4x4f* result, const float x, const float y, const float z) {
    result->m[0] = 1.0f;
    result->m[1] = 0.0f;
    result->m[2] = 0.0f;
    result->m[3] = 0.0f;
    result->m[4] = 0.0f;
    result->m[5] = 1.0f;
    result->m[6] = 0.0f;
    result->m[7] = 0.0f;
    result->m[8] = 0.0f;
    result->m[9] = 0.0f;
    result->m[10] = 1.0f;
    result->m[11] = 0.0f;
    result->m[12] = x;
    result->m[13] = y;
    result->m[14] = z;
    result->m[15] = 1.0f;
}

// Use left-multiplication to accumulate transformations.
static inline void
XrMatrix4x4f_Multiply(XrMatrix4x4f* result, const XrMatrix4x4f* a, const XrMatrix4x4f* b) {
    result->m[0] = a->m[0] * b->m[0] + a->m[4] * b->m[1] + a->m[8] * b->m[2] + a->m[12] * b->m[3];
    result->m[1] = a->m[1] * b->m[0] + a->m[5] * b->m[1] + a->m[9] * b->m[2] + a->m[13] * b->m[3];
    result->m[2] = a->m[2] * b->m[0] + a->m[6] * b->m[1] + a->m[10] * b->m[2] + a->m[14] * b->m[3];
    result->m[3] = a->m[3] * b->m[0] + a->m[7] * b->m[1] + a->m[11] * b->m[2] + a->m[15] * b->m[3];

    result->m[4] = a->m[0] * b->m[4] + a->m[4] * b->m[5] + a->m[8] * b->m[6] + a->m[12] * b->m[7];
    result->m[5] = a->m[1] * b->m[4] + a->m[5] * b->m[5] + a->m[9] * b->m[6] + a->m[13] * b->m[7];
    result->m[6] = a->m[2] * b->m[4] + a->m[6] * b->m[5] + a->m[10] * b->m[6] + a->m[14] * b->m[7];
    result->m[7] = a->m[3] * b->m[4] + a->m[7] * b->m[5] + a->m[11] * b->m[6] + a->m[15] * b->m[7];

    result->m[8] = a->m[0] * b->m[8] + a->m[4] * b->m[9] + a->m[8] * b->m[10] + a->m[12] * b->m[11];
    result->m[9] = a->m[1] * b->m[8] + a->m[5] * b->m[9] + a->m[9] * b->m[10] + a->m[13] * b->m[11];
    result->m[10] =
        a->m[2] * b->m[8] + a->m[6] * b->m[9] + a->m[10] * b->m[10] + a->m[14] * b->m[11];
    result->m[11] =
        a->m[3] * b->m[8] + a->m[7] * b->m[9] + a->m[11] * b->m[10] + a->m[15] * b->m[11];

    result->m[12] =
        a->m[0] * b->m[12] + a->m[4] * b->m[13] + a->m[8] * b->m[14] + a->m[12] * b->m[15];
    result->m[13] =
        a->m[1] * b->m[12] + a->m[5] * b->m[13] + a->m[9] * b->m[14] + a->m[13] * b->m[15];
    result->m[14] =
        a->m[2] * b->m[12] + a->m[6] * b->m[13] + a->m[10] * b->m[14] + a->m[14] * b->m[15];
    result->m[15] =
        a->m[3] * b->m[12] + a->m[7] * b->m[13] + a->m[11] * b->m[14] + a->m[15] * b->m[15];
}

static inline void XrMatrix4x4f_Transpose(XrMatrix4x4f* result, const XrMatrix4x4f* src) {
    result->m[0] = src->m[0];
    result->m[1] = src->m[4];
    result->m[2] = src->m[8];
    result->m[3] = src->m[12];
    result->m[4] = src->m[1];
    result->m[5] = src->m[5];
    result->m[6] = src->m[9];
    result->m[7] = src->m[13];
    result->m[8] = src->m[2];
    result->m[9] = src->m[6];
    result->m[10] = src->m[10];
    result->m[11] = src->m[14];
    result->m[12] = src->m[3];
    result->m[13] = src->m[7];
    result->m[14] = src->m[11];
    result->m[15] = src->m[15];
}

static inline void XrMatrix4x4f_InvertRigidBody(XrMatrix4x4f* result, const XrMatrix4x4f* src) {
    result->m[0] = src->m[0];
    result->m[1] = src->m[4];
    result->m[2] = src->m[8];
    result->m[3] = 0.0f;
    result->m[4] = src->m[1];
    result->m[5] = src->m[5];
    result->m[6] = src->m[9];
    result->m[7] = 0.0f;
    result->m[8] = src->m[2];
    result->m[9] = src->m[6];
    result->m[10] = src->m[10];
    result->m[11] = 0.0f;
    result->m[12] = -(src->m[0] * src->m[12] + src->m[1] * src->m[13] + src->m[2] * src->m[14]);
    result->m[13] = -(src->m[4] * src->m[12] + src->m[5] * src->m[13] + src->m[6] * src->m[14]);
    result->m[14] = -(src->m[8] * src->m[12] + src->m[9] * src->m[13] + src->m[10] * src->m[14]);
    result->m[15] = 1.0f;
}

static inline XrMatrix4x4f XrMatrix4x4f_CreateFromRigidTransform(const XrPosef* s) {
    XrMatrix4x4f rotationMatrix;
    XrMatrix4x4f_CreateFromQuaternion(&rotationMatrix, &s->orientation);

    XrMatrix4x4f translationMatrix;
    XrMatrix4x4f_CreateTranslation(&translationMatrix, s->position.x, s->position.y, s->position.z);

    XrMatrix4x4f r;
    XrMatrix4x4f_Multiply(&r, &translationMatrix, &rotationMatrix);
    return r;
}

// Creates a projection matrix based on the specified dimensions.
// The projection matrix transforms -Z=forward, +Y=up, +X=right to the appropriate clip space for
// the graphics API. The far plane is placed at infinity if farZ <= nearZ. An infinite projection
// matrix is preferred for rasterization because, except for things *right* up against the near
// plane, it always provides better precision:
//              "Tightening the Precision of Perspective Rendering"
//              Paul Upchurch, Mathieu Desbrun
//              Journal of Graphics Tools, Volume 16, Issue 1, 2012
static inline void XrMatrix4x4f_CreateProjection(
    XrMatrix4x4f* result,
    GraphicsAPI graphicsApi,
    const float tanAngleLeft,
    const float tanAngleRight,
    const float tanAngleUp,
    float const tanAngleDown,
    const float nearZ,
    const float farZ) {
    const float tanAngleWidth = tanAngleRight - tanAngleLeft;

    // Set to tanAngleDown - tanAngleUp for a clip space with positive Y down (Vulkan).
    // Set to tanAngleUp - tanAngleDown for a clip space with positive Y up (OpenGL / D3D / Metal).
    const float tanAngleHeight = (graphicsApi == GRAPHICS_VULKAN) ? (tanAngleDown - tanAngleUp)
                                                                  : (tanAngleUp - tanAngleDown);

    // Set to nearZ for a [-1,1] Z clip space (OpenGL / OpenGL ES).
    // Set to zero for a [0,1] Z clip space (Vulkan / D3D / Metal).
    const float offsetZ =
        (graphicsApi == GRAPHICS_OPENGL || graphicsApi == GRAPHICS_OPENGL_ES) ? nearZ : 0.0f;
    if (farZ <= nearZ) {
        // place the far plane at infinity
        result->m[0] = 2 / tanAngleWidth;
        result->m[4] = 0;
        result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
        result->m[12] = 0;

        result->m[1] = 0;
        result->m[5] = 2 / tanAngleHeight;
        result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
        result->m[13] = 0;

        result->m[2] = 0;
        result->m[6] = 0;
        result->m[10] = -1;
        result->m[14] = -(nearZ + offsetZ);

        result->m[3] = 0;
        result->m[7] = 0;
        result->m[11] = -1;
        result->m[15] = 0;
    } else {
        // normal projection
        result->m[0] = 2 / tanAngleWidth;
        result->m[4] = 0;
        result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
        result->m[12] = 0;

        result->m[1] = 0;
        result->m[5] = 2 / tanAngleHeight;
        result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
        result->m[13] = 0;

        result->m[2] = 0;
        result->m[6] = 0;
        result->m[10] = -(farZ + offsetZ) / (farZ - nearZ);
        result->m[14] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

        result->m[3] = 0;
        result->m[7] = 0;
        result->m[11] = -1;
        result->m[15] = 0;
    }
}

// Creates a projection matrix based on the specified FOV (specified in radians).
static inline void XrMatrix4x4f_CreateProjectionFov(
    XrMatrix4x4f* result,
    GraphicsAPI graphicsApi,
    const XrFovf fov,
    const float nearZ,
    const float farZ) {
    const float tanLeft = tanf(fov.angleLeft);
    const float tanRight = tanf(fov.angleRight);

    const float tanDown = tanf(fov.angleDown);
    const float tanUp = tanf(fov.angleUp);

    XrMatrix4x4f_CreateProjection(
        result, graphicsApi, tanLeft, tanRight, tanUp, tanDown, nearZ, farZ);
}

// Creates a rotation matrix.
// If -Z=forward, +Y=up, +X=right, then degreesX=pitch, degreesY=yaw, degreesZ=roll.
static inline void XrMatrix4x4f_CreateRotation(
    XrMatrix4x4f* result,
    const float radiansX,
    const float radiansY,
    const float radiansZ) {
    const float sinX = sinf(radiansX);
    const float cosX = cosf(radiansX);
    const XrMatrix4x4f rotationX = {
        {1.0f,
         0.0f,
         0.0f,
         0.0f,
         0.0f,
         cosX,
         sinX,
         0.0f,
         0.0f,
         -sinX,
         cosX,
         0.0f,
         0.0f,
         0.0f,
         0.0f,
         1.0f}};

    const float sinY = sinf(radiansY);
    const float cosY = cosf(radiansY);
    const XrMatrix4x4f rotationY = {
        {cosY,
         0,
         -sinY,
         0.0f,
         0.0f,
         1.0f,
         0.0f,
         0.0f,
         sinY,
         0.0f,
         cosY,
         0.0f,
         0.0f,
         0.0f,
         0.0f,
         1.0f}};

    const float sinZ = sinf(radiansZ);
    const float cosZ = cosf(radiansZ);
    const XrMatrix4x4f rotationZ = {
        {cosZ,
         sinZ,
         0.0f,
         0.0f,
         -sinZ,
         cosZ,
         0.0f,
         0.0f,
         0.0f,
         0.0f,
         1.0f,
         0.0f,
         0.0f,
         0.0f,
         0.0f,
         1.0f}};

    XrMatrix4x4f rotationXY;
    XrMatrix4x4f_Multiply(&rotationXY, &rotationY, &rotationX);
    XrMatrix4x4f_Multiply(result, &rotationZ, &rotationXY);
}

#endif // OPENXR_OCULUS_HELPERS_H
