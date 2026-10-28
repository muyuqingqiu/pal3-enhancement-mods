#ifndef PAL3_CAMERA_GEOMETRY_H
#define PAL3_CAMERA_GEOMETRY_H
struct gbVec3D;
void PAL3CameraGeometry_Select(const char *world, const char *scene);
int PAL3CameraGeometry_Ready(void);
float PAL3CameraGeometry_MapDistance(const struct gbVec3D *target, float fov, float aspect);
float PAL3CameraGeometry_HitDistance(const struct gbVec3D *from,
    const struct gbVec3D *direction, float limit);
#endif
