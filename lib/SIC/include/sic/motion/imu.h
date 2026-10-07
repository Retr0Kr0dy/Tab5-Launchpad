#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float ax, ay, az; float gx, gy, gz; } sic_imu_sample_t; /* accel in g, gyro in deg/s */

struct imu_vtbl_s { int (*read)(const void* self, sic_imu_sample_t* out); };
typedef struct imu_s { const struct imu_vtbl_s* v; void* impl; } imu_t;

typedef struct sic_imu_cfg_s { int i2c_bus; uint8_t i2c_addr; } sic_imu_cfg_t;

const imu_t* sic_imu(int index);

#ifdef __cplusplus
}
#endif
