// SPDX-License-Identifier: MPL-2.0
#include <embree4/rtcore.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static bool point_query(RTCPointQueryFunctionArguments *args) {
    ++*static_cast<unsigned *>(args->userPtr);
    return false;
}

int main() {
    RTCDevice device = rtcNewDevice("isa=sse2,threads=1");
    if (!device) {
        std::fprintf(stderr, "rtcNewDevice failed\n");
        return 2;
    }
    RTCScene scene = rtcNewScene(device);
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
    auto *vertices = static_cast<float *>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float), 3));
    const float data[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(vertices, data, sizeof(data));
    auto *indices = static_cast<unsigned *>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(unsigned), 1));
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    rtcCommitGeometry(geom);
    const unsigned geometry_id = rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);
    rtcCommitScene(scene);

    RTCRayHit rayhit{};
    rayhit.ray.org_x = rayhit.ray.org_y = .2f;
    rayhit.ray.org_z = -1.f;
    rayhit.ray.dir_z = 1.f;
    rayhit.ray.tfar = std::numeric_limits<float>::infinity();
    rayhit.ray.mask = ~0u;
    rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    for (auto &id : rayhit.hit.instID)
        id = RTC_INVALID_GEOMETRY_ID;
    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);
    rtcIntersect1(scene, &rayhit, &args);
    std::printf("intersect geomID=%u distance=%f\n", rayhit.hit.geomID, rayhit.ray.tfar);

    RTCPointQuery query{};
    query.x = query.y = .2f;
    query.radius = 1.f;
    RTCPointQueryContext context;
    rtcInitPointQueryContext(&context);
    unsigned count = 0;
    rtcPointQuery(scene, &query, &context, point_query, &count);
    std::printf("point query callbacks=%u\n", count);
    RTCError error = rtcGetDeviceError(device);
    rtcReleaseScene(scene);
    rtcReleaseDevice(device);
    return error == RTC_ERROR_NONE && count == 1 && rayhit.hit.geomID == geometry_id &&
                   std::abs(rayhit.ray.tfar - 1.f) < 1e-6f
               ? 0
               : 1;
}
