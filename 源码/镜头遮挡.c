#define USE_PAL3_DEFINITIONS
#include "PAL3patch.h"
#include "镜头遮挡.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* 保留地图范围与镜头碰撞数据；不再接管场景网格绘制。
 * 一次命中就隐藏整个材质组会让山体和地面缺块，逐绘制检测也会拖慢低角度画面。 */
struct camera_triangle { struct gbVec3D a, b, c; };
struct camera_node {
    float lower[3], upper[3];
    uint32_t first, count, left, right;
};
static struct camera_node *nodes;
static struct camera_triangle *triangles;
static uint32_t node_count;
static char selected_world[33], selected_scene[33];

static int copy_scene_name(char output[33], const char *input)
{
    int i;
    for (i = 0; i < 32 && input[i]; ++i) {
        unsigned char c = input[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_')) return 0;
        output[i] = c;
    }
    output[i] = 0;
    return i > 0 && i < 32;
}

void PAL3CameraGeometry_Select(const char *world, const char *scene)
{
    char world_name[33], scene_name[33], path[MAX_PATH], *end;
    HANDLE file;
    DWORD length, got, size;
    uint32_t header[4], i;
    unsigned char *data;
    uint64_t expected;
    if (!copy_scene_name(world_name, world) || !copy_scene_name(scene_name, scene)) {
        free(nodes); nodes = NULL; triangles = NULL; node_count = 0;
        selected_world[0] = selected_scene[0] = 0;
        return;
    }
    if (!_stricmp(selected_world, world_name) && !_stricmp(selected_scene, scene_name)) return;
    free(nodes); nodes = NULL; triangles = NULL; node_count = 0;
    strcpy(selected_world, world_name); strcpy(selected_scene, scene_name);
    length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path) || !(end = strrchr(path, '\\'))) return;
    if (snprintf(end + 1, sizeof(path) - (end + 1 - path), "hd\\camera\\%s\\%s.p3cm",
            world_name, scene_name) >= sizeof(path) - (end + 1 - path)) return;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(file, NULL);
    if (!ReadFile(file, header, sizeof(header), &got, NULL) || got != sizeof(header) ||
        memcmp(header, "P3CM", 4) || header[1] != 1 ||
        header[2] > 1000000 || header[3] > 1000000) { CloseHandle(file); return; }
    expected = sizeof(header) + (uint64_t)header[2] * sizeof(struct camera_node) +
        (uint64_t)header[3] * sizeof(struct camera_triangle);
    if (expected != size || size > 64u * 1024u * 1024u || !header[2]) { CloseHandle(file); return; }
    data = malloc(size - sizeof(header));
    if (!data) { CloseHandle(file); return; }
    if (!ReadFile(file, data, size - sizeof(header), &got, NULL) || got != size - sizeof(header)) {
        free(data); CloseHandle(file); return;
    }
    CloseHandle(file);
    nodes = (struct camera_node *)data;
    for (i = 0; i < header[2]; ++i) {
        struct camera_node *n = &nodes[i];
        if ((n->count && (n->first > header[3] || n->count > header[3] - n->first)) ||
            (!n->count && (n->left <= i || n->left >= header[2] ||
                          n->right <= i || n->right >= header[2]))) {
            free(nodes); nodes = NULL; return;
        }
    }
    triangles = (struct camera_triangle *)(data + header[2] * sizeof(struct camera_node));
    node_count = header[2];
#if PAL3_MENU_TREE_DIAG
    plog("[PAL3CameraGeometry] %s/%s nodes=%u triangles=%u bytes=%lu",
        world_name, scene_name, header[2], header[3], size);
#endif
}

static struct gbVec3D subtract(struct gbVec3D a, struct gbVec3D b)
{
    return (struct gbVec3D){a.x-b.x, a.y-b.y, a.z-b.z};
}
static struct gbVec3D cross(struct gbVec3D a, struct gbVec3D b)
{
    return (struct gbVec3D){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
static float dot(struct gbVec3D a, struct gbVec3D b) { return a.x*b.x+a.y*b.y+a.z*b.z; }

static float hit_node(uint32_t index, const struct gbVec3D *origin,
    const struct gbVec3D *direction, float limit)
{
    const struct camera_node *n = &nodes[index];
    const float from[3] = {origin->x, origin->y, origin->z};
    const float ray[3] = {direction->x, direction->y, direction->z};
    float entry_distance = 0, exit_distance = limit;
    uint32_t i;
    for (i = 0; i < 3; ++i) {
        if (fabsf(ray[i]) < 0.000001f) {
            if (from[i] < n->lower[i] || from[i] > n->upper[i]) return limit;
        } else {
            float a = (n->lower[i]-from[i])/ray[i], b = (n->upper[i]-from[i])/ray[i];
            if (a > b) { float swap=a; a=b; b=swap; }
            if (a > entry_distance) entry_distance=a;
            if (b < exit_distance) exit_distance=b;
            if (entry_distance > exit_distance) return limit;
        }
    }
    if (!n->count) {
        limit = hit_node(n->left, origin, direction, limit);
        return hit_node(n->right, origin, direction, limit);
    }
    for (i = 0; i < n->count; ++i) {
        const struct camera_triangle *t = &triangles[n->first+i];
        struct gbVec3D e1=subtract(t->b,t->a), e2=subtract(t->c,t->a);
        struct gbVec3D h=cross(*direction,e2), s, q;
        float det=dot(e1,h), inverse, u, v, distance;
        if (fabsf(det) < 0.000001f) continue;
        inverse=1.0f/det; s=subtract(*origin,t->a); u=inverse*dot(s,h);
        if (u < 0 || u > 1) continue;
        q=cross(s,e1); v=inverse*dot(*direction,q);
        if (v < 0 || u+v > 1) continue;
        distance=inverse*dot(e2,q);
        /* 原版取点树来自行走面；这里检测真实 POL 山墙，且只接受人物到镜头这一段。 */
        if (distance > 1.0f && distance < limit) limit=distance;
    }
    return limit;
}

float PAL3CameraGeometry_HitDistance(const struct gbVec3D *from,
    const struct gbVec3D *direction, float limit)
{
    return node_count ? hit_node(0, from, direction, limit) : limit;
}

int PAL3CameraGeometry_Ready(void) { return node_count != 0; }

float PAL3CameraGeometry_MapDistance(const struct gbVec3D *target, float fov, float aspect)
{
    float radius = 0, angle;
    int i;
    if (!node_count || fov <= 0.05f || fov >= 179.0f || aspect <= 0.0f) return 0;
    /* gbCamera 保存的是角度，例如璧山为 26 度；三角函数使用弧度。 */
    fov *= 0.01745329252f;
    /* 用人物到场景最远角点的包围球，覆盖不同长宽地图；窄屏以较小视角计算。 */
    for (i = 0; i < 8; ++i) {
        float x = ((i & 1) ? nodes[0].upper[0] : nodes[0].lower[0]) - target->x;
        float y = ((i & 2) ? nodes[0].upper[1] : nodes[0].lower[1]) - target->y;
        float z = ((i & 4) ? nodes[0].upper[2] : nodes[0].lower[2]) - target->z;
        float distance = sqrtf(x*x + y*y + z*z);
        if (distance > radius) radius = distance;
    }
    angle = fov * 0.5f;
    if (aspect < 1.0f) angle = atanf(tanf(angle) * aspect);
    return radius * 1.05f / sinf(angle);
}
