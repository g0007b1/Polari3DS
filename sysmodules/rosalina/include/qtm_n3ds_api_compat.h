#pragma once

/*
 * Polari n3ds.c uses QTM service APIs added in newer libctru.
 * pablomk7/luma3dsbuildtools ships older headers: duplicate the public
 * types/prototypes from devkitPro libctru qtm.h when missing.
 */
#include <3ds/types.h>
#include <stdbool.h>

#ifndef QTM_CAL_CFG_BLK_ID

#define QTM_STATUS_CFG_BLK_ID 0x180000u
#define QTM_CAL_CFG_BLK_ID    0x180001u

typedef enum {
    QTM_STATUS_ENABLED = 0,
    QTM_STATUS_SS3D_DISABLED = 1,
    QTM_STATUS_UNAVAILABLE = 2,
} QtmStatus;

typedef struct {
    float centerBarrierPosition;
    float translationX;
    float translationY;
    float rotationZ;
    float fovX;
    float viewingDistance;
} QtmCalibrationData;

typedef enum {
    QTM_EYE_LEFT = 0,
    QTM_EYE_RIGHT = 1,
    QTM_EYE_NUM,
} QtmEyeSide;

typedef struct {
    bool headTracked;
    bool faceDetected;
    bool eyesDetected;
    u8 _unused;
    bool clamped;
    u8 _padding[3];
    float confidenceLevel;
    float eyeCameraCoordinates[QTM_EYE_NUM][2];
    float eyeWorldCoordinates[QTM_EYE_NUM][2];
    float dPitch;
    float dYaw;
    float dRoll;
    s64 samplingTick;
} QtmTrackingData;

Result QTMS_GetQtmStatus(QtmStatus *outQtmStatus);
Result QTMU_IsCurrentAppBlacklisted(bool *outBlacklisted);
Result QTMS_SetQtmStatus(QtmStatus qtmStatus);
Result QTMS_DisableAutoBarrierControl(void);
Result QTMS_GetCurrentBarrierPosition(u8 *outPosition);
Result QTMS_SetBarrierPosition(u8 position);
Result QTMS_EnableAutoBarrierControl(void);
Result QTMU_GetTrackingData(QtmTrackingData *outData);
float qtmEstimateEyeToCameraDistance(const QtmTrackingData *data);
Result QTMS_SetCalibrationData(const QtmCalibrationData *cal, bool saveCalToCfg);

#endif /* !QTM_CAL_CFG_BLK_ID */
