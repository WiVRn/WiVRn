// Copyright® 2015-2023 PICO Technology Co., Ltd. All rights reserved.
// This plugin incorporates portions of the Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.
// Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved. 

#ifndef PXR_TYPES_H
#define PXR_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#ifndef VK_VERSION_1_0
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
#endif
#define PHF_LENGTH 50
#define PXR_UUID_SIZE 2
static const int PXR_MAX_EVENT_COUNT = 20;
#define PXRP_SUCCESS(result) ((result) >= 0)
#define PXRP_FAILURE(result) ((result) < 0)

static const int XR_ERROR_EYEGAZY_SWITCH_NOT_ENABLE_BD            =  1000030101;
static const int XR_ERROR_EYEGAZY_PERMISION_NOT_ENABLE_BD         =  1000030102;
static const int XR_ERROR_FACIAL_TRACKING_SWITCH_NOT_ENABLE_BD    =  1000386101;
static const int XR_ERROR_FACIAL_TRACKING_FT_PERMISION_NOT_ENABLE_BD     =  1000386102;
static const int XR_ERROR_FACIAL_TRACKING_MIC_PERMISION_NOT_ENABLE_BD    =  1000386103;

typedef enum
{
    PXR_RET_SUCCESS = 0,
    PXR_RET_FAIL = -1,
    PXR_RET_INVALID_LAYER_ID = -2,
    PXR_RET_INVALID_IMAGE_INDEX = -3,
    PXR_RET_INPUT_POINTER_IS_NULL = -4,
    PXR_RET_INIT_SESSION_IS_NULL = -5,
    PXR_RET_INVALID_INPUT_PARAM = -6,
    PXR_RET_NOT_BEGIN_XR = -7,
    PXR_RET_NOT_IMPLEMENTED = -8,
}PxrReturnStatus;

typedef enum
{
    PXR_LAYER_PROJECTION = 0,
    PXR_LAYER_QUAD = 1,
    PXR_LAYER_CYLINDER = 2,
    PXR_LAYER_EQUIRECT = 3,
    PXR_LAYER_EQUIRECT2 = 4,
    PXR_LAYER_CUBE = 5,
    PXR_LAYER_EAC = 6,
    PXR_LAYER_FISHEYE = 7,
    PXR_LAYER_VST_MASK = 8,
} PxrLayerShape;

typedef enum
{
    PXR_LAYER_LAYOUT_STEREO = 0,
    PXR_LAYER_LAYOUT_DOUBLE_WIDE = 1,
    PXR_LAYER_LAYOUT_ARRAY = 2,
    PXR_LAYER_LAYOUT_MONO = 3
}PxrLayerLayout;

typedef enum
{
    PXR_LAYER_FLAG_ANDROID_SURFACE = 1 << 0,
    PXR_LAYER_FLAG_PROTECTED_CONTENT = 1 << 1,
    PXR_LAYER_FLAG_STATIC_IMAGE = 1 << 2,
    PXR_LAYER_FLAG_USE_EXTERNAL_IMAGES = 1 << 4,
    PXR_LAYER_FLAG_3D_LEFT_RIGHT_SURFACE = 1 << 5,
    PXR_LAYER_FLAG_3D_TOP_BOTTOM_SURFACE = 1 << 6,
    PXR_LAYER_FLAG_ENABLE_FRAME_EXTRAPOLATION = 1 << 7,
    PXR_LAYER_FLAG_ENABLE_SUBSAMPLED = 1 << 8,
    PXR_LAYER_FLAG_ENABLE_FRAME_EXTRAPOLATION_PTW = 1 << 9,
    PXR_LAYER_FLAG_SHARED_IMAGES_BETWEEN_LAYERS = 1 << 10
} PxrLayerCreateFlags;

typedef enum
{
    PXR_LAYER_FLAG_NO_COMPOSITION_DEPTH_TESTING = 1 << 3,
    PXR_LAYER_FLAG_USE_EXTERNAL_HEAD_POSE = 1 << 5,
    PXR_LAYER_FLAG_LAYER_POSE_NOT_IN_TRACKING_SPACE = 1 << 6,
    PXR_LAYER_FLAG_HEAD_LOCKED = 1 << 7,
    PXR_LAYER_FLAG_USE_EXTERNAL_IMAGE_INDEX = 1 << 8,
    PXR_LAYER_FLAG_PRESENTATION_PROTECTION = 1 << 9,
    PXR_LAYER_FLAG_SOURCE_ALPHA_1_0 = 1 << 10,
    PXR_LAYER_FLAG_USE_FRAME_EXTRAPOLATION = 1 << 11,
    PXR_LAYER_FLAG_QUICK_SEETHROUGH = 1 << 12,
    PXR_LAYER_FLAG_ENABLE_NORMAL_SUPER_SAMPLING = 1 << 13,
    PXR_LAYER_FLAG_ENABLE_QUALITY_SUPER_SAMPLING = 1 << 14,
    PXR_LAYER_FLAG_ENABLE_NORMAL_SHARPENING = 1 << 15,
    PXR_LAYER_FLAG_ENABLE_QUALITY_SHARPENING = 1 << 16,
    PXR_LAYER_FLAG_ENABLE_FIXED_FOVEATED_SUPER_SAMPLING = 1<<17,
    PXR_LAYER_FLAG_ENABLE_FIXED_FOVEATED_SHARPENING = 1 << 18,
    PXR_LAYER_FLAG_ENABLE_SELF_ADAPTIVE_SHARPENING = 1 << 19,
    PXR_LAYER_FLAG_PREMULTIPLIED_ALPHA       = 1 << 20,
    PXR_LAYER_FLAG_ENABLE_SUPER_RESOLUTION = 1 << 21,
    PXR_LAYER_FLAG_COLOR_SPACE_HDR_PQ = 1 << 22,
    PXR_LAYER_FLAG_COLOR_SPACE_HDR_HLG = 1 << 23,
    PXR_LAYER_FLAG_ENABLE_NEUR_SUPER_RESOLUTION = 1 << 24,
} PxrLayerSubmitFlags;

typedef enum
{
    PXR_FEATURE_MULTIVIEW = 0,
    PXR_FEATURE_FOVEATION = 1,
    PXR_FEATURE_EYETRACKING = 2
} PxrFeatureType;

typedef enum
{
    PXR_UNITY = 0,
    PXR_UNREAL = 1,
    PXR_NATIVE = 2
} PxrPlatformOption;

typedef enum
{
    PXR_OPENGL_ES = 0,
    PXR_VULKAN
} PxrGraphicOption;

typedef enum
{
    PXR_EYE_LEFT = 0,
    PXR_EYE_RIGHT = 1,
    PXR_EYE_BOTH = 2,
    PXR_EYE_MAX = 2
} PxrEyeType;

typedef enum
{
    PXR_LOG_VERBOSE = 2,
    PXR_LOG_DEBUG,
    PXR_LOG_INFO,
    PXR_LOG_WARN,
    PXR_LOG_ERROR,
    PXR_LOG_FATAL,
} PxrLogPriority;

typedef enum
{
    PXR_FOVEATION_LEVEL_NONE = -1,
    PXR_FOVEATION_LEVEL_LOW = 0,
    PXR_FOVEATION_LEVEL_MID = 1,
    PXR_FOVEATION_LEVEL_HIGH = 2,
    PXR_FOVEATION_LEVEL_TOP_HIGH = 3
} PxrFoveationLevel;

typedef enum
{
    PXR_COLOR_SPACE_LINEAR = 0,
    PXR_COLOR_SPACE_SRGB = 1
} PxrColorSpace;

typedef enum
{
    PXR_FILTER_TYPE_NEAREST = 0,
    PXR_FILTER_TYPE_LINEAR = 1,
    PXR_FILTER_TYPE_CUBIC = 2
}PxrFilterType;

typedef enum
{
    PXR_RENDER_TEXTURE_WIDTH = 0,
    PXR_RENDER_TEXTURE_HEIGHT,
    PXR_SHOW_FPS,
    PXR_RUNTIME_LOG_LEVEL,
    PXR_PXRPLUGIN_LOG_LEVEL,
    PXR_UNITY_LOG_LEVEL,
    PXR_UNREAL_LOG_LEVEL,
    PXR_NATIVE_LOG_LEVEL,
    PXR_TARGET_FRAME_RATE,
    PXR_NECK_MODEL_X,
    PXR_NECK_MODEL_Y,
    PXR_NECK_MODEL_Z,
    PXR_DISPLAY_REFRESH_RATE,
    PXR_ENABLE_6DOF,
    PXR_CONTROLLER_TYPE,
    PXR_PHYSICAL_IPD,
    PXR_TO_DELTA_SENSOR_Y,
    PXR_GET_DISPLAY_RATE,
    PXR_FOVEATION_SUBSAMPLED_ENABLED,
    PXR_TRACKING_ORIGIN_HEIGHT,
    PXR_ENGINE_VERSION,
    PXR_UNREAL_OPENGL_NOERROR,
    PXR_ENABLE_CPT,
    PXR_MRC_TEXTURE_ID,
    PXR_RENDER_FPS,
    PXR_MSAA_LEVEL_RECOMMENDED,
    PXR_MRC_TEXTURE_ID_2,
    PXR_SET_SURFACE_VIEW,
    PXR_API_VERSION,
    PXR_MRC_POSITION_Y_OFFSET,
    PXR_MRC_TEXTURE_WIDTH,
    PXR_MRC_TEXTURE_HEIGHT,
    PXR_SINGLEPASS,
    PXR_FOVLEVEL,
    PXR_ANDROID_SURFACE_DIMENSIONS,
    PXR_ANDROID_SN,
    PXR_SET_DESIRED_FPS,
    PXR_GET_SEETHROUGH_STATE,
    PXR_SET_LAYER_BLEND,
    PXR_LEFT_EYE_FOV,
    PXR_RIGHT_EYE_FOV,
    PXR_BOTH_EYE_FOV,
    PXR_SUPPORT_QUICK_SEETHROUGH,
    PXR_SET_FILTER_TYPE,
    PXR_SET_SUBMIT_LAYER_EXT_ITEM_COLOR_MATRIX,
    PXR_VST_STATE_INFO = 20230710,
} PxrConfigType;

typedef enum
{
    PXR_RESET_POSITION = 0,
    PXR_RESET_ORIENTATION = 1,
    PXR_RESET_ORIENTATION_Y_ONLY = 2,
    PXR_RESET_ALL
} PxrResetSensorOption;

typedef enum
{
    PXR_TYPE_UNKNOWN = 0,
    PXR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING = 1,
    PXR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED = 2,
    PXR_TYPE_EVENT_DATA_EVENTS_LOST = 3,
    PXR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED = 4,
    PXR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT = 5,
    PXR_TYPE_EVENT_DATA_CONTROLLER = 6,
    PXR_TYPE_EVENT_DATA_SESSION_STATE_READY=7,
    PXR_TYPE_EVENT_DATA_SESSION_STATE_STOPPING=8,
    PXR_TYPE_EVENT_DATA_SEETHROUGH_STATE_CHANGED = 9,
    PXR_TYPE_EVENT_HARDIPD_STATE_CHANGED = 10,
    PXR_TYPE_EVENT_FOVEATION_LEVEL_CHANGED = 11,
    PXR_TYPE_EVENT_FRUSTUM_STATE_CHANGED = 12,
    PXR_TYPE_EVENT_RENDER_TEXTURE_CHANGED = 13,
    PXR_TYPE_EVENT_TARGET_FRAME_RATE_STATE_CHANGED = 14,
    PXR_TYPE_EVENT_DATA_HMD_KEY = 15,
    PXR_TYPE_EVENT_DATA_MRC_STATUS = 16,
    PXR_TYPE_EVENT_DATA_REFRESH_RATE_CHANGED = 17,
    PXR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX = 18,

    PXR_TYPE_EVENT_SPATIAL_ANCHOR_SAVE_RESULT = 20,
    PXR_TYPE_EVENT_SPATIAL_ANCHOR_DELETE_RESULT = 21,
    PXR_TYPE_EVENT_SPATIAL_ANCHOR_LOAD_RESULTS = 22,
    PXR_TYPE_EVENT_SPATIAL_MODEL_SAVE_RESULT = 23,
    PXR_TYPE_EVENT_SPATIAL_MODEL_DELETE_RESULT = 24,
    PXR_TYPE_EVENT_SPATIAL_MODEL_LOAD_RESULTS = 25,
    PXR_TYPE_EVENT_SPATIAL_INSTANCE_PERSISTENCE_EXPORT_COMPLETE = 26,
    PXR_TYPE_EVENT_SPATIAL_INSTANCE_PERSISTENCE_IMPORT_COMPLETE = 27,
    PXR_TYPE_EVENT_NEW_SPACE_READY  = 28,
    PXR_TYPE_EVENT_SPATIAL_ANCHOR_LOAD_RESULTS_AVAILABLE = 29,
    PXR_TYPE_EVENT_SPATIAL_ANCHOR_LOAD_RESULTS_COMPLETE = 30,
//    PXR_TYPE_EVENT_SPACE_OPTIMIZED_STATUS  = 31,
    PXR_TYPE_EVENT_ROOM_SCENE_DATA_SAVE_RESULT = 31,
    PXR_TYPE_EVENT_ROOM_SCENE_DATA_DELETE_RESULT = 32,
    PXR_TYPE_EVENT_ROOM_SCENE_LOAD_RESULTS_COMPLETE = 33,
    PXR_TYPE_EVENT_HMD_BATTERY_CHANGED = 34,
    PXR_TYPE_EVENT_DATA_SDK_LOGLEVEL_CHANGED = 35,

    //PXR_TYPE_EVENT_SEMI_AUTO_ROOM_CAPTURE_CANDIDATES_UPDATED = 36,

    PXR_TYPE_EVENT_ROOM_SCENE_DATA_UPDATE_RESULT = 37,
    PXR_TYPE_EVENT_TRACKING_STATE_CHANGED = 40,
    //mr v2.0 MR reserved 100-200
    PXR_TYPE_EVENT_MR_MIN = 100,
    PXR_TYPE_EVENT_DATA_SPATIAL_TRACKING_STATE_UPDATE = 101,
    PXR_TYPE_SYSTEM_ANCHOR_ENTITY_PROPERTIES = 102,
    PXR_TYPE_ANCHOR_ENTITY_CREATE_INFO = 103,
    PXR_TYPE_ANCHOR_ENTITY_DESTROY_INFO = 104,
    PXR_TYPE_ANCHOR_SPACE_CREATE_INFO = 105,
    PXR_TYPE_ANCHOR_COMPONENT_SCENE_LABEL_INFO = 106,
    PXR_TYPE_ANCHOR_COMPONENT_PLANE_INFO = 107,
    PXR_TYPE_ANCHOR_COMPONENT_BOX_INFO = 108,//Same with VOLUME
    PXR_TYPE_ANCHOR_COMPONENT_ADD_INFO = 109,
    PXR_TYPE_ANCHOR_COMPONENT_REMOVE_INFO = 110,
    PXR_TYPE_ANCHOR_PLANE_BOUNDARY_INFO = 111,
    PXR_TYPE_ANCHOR_PLANE_POLYGON_INFO = 112,
    PXR_TYPE_ANCHOR_BOX_INFO = 113,
    PXR_TYPE_ANCHOR_ENTITY_PERSIST_INFO = 114,
    PXR_TYPE_ANCHOR_ENTITY_UNPERSIST_INFO = 115,
    PXR_TYPE_ANCHOR_ENTITY_LIST = 116,
    PXR_TYPE_ANCHOR_ENTITY_CLEAR_INFO = 117,
    PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_PERSISTED = 118,
    PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_UNPERSISTED = 119,
    PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_CLEARED = 120,
    PXR_TYPE_ANCHOR_ENTITY_LOAD_INFO = 121,
    PXR_TYPE_ANCHOR_ENTITY_LOAD_UUID_FILTER =122,
    PXR_TYPE_ANCHOR_ENTITY_LOAD_COMPONENT_FILTER = 123,
    PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_LOADED = 124,
    PXR_TYPE_ANCHOR_ENTITY_LOAD_RESULT = 125,
    PXR_TYPE_SPATIAL_SCENE_CAPTURE_START_INFO = 126,
    PXR_TYPE_EVENT_DATA_SPATIAL_SCENE_CAPTURED = 127,
    PXR_TYPE_ANCHOR_ENTITY_LOAD_SPATIAL_SCENE_FILTER = 128,
    PXR_TYPE_EVENT_SEMI_AUTO_ROOM_CAPTURE_CANDIDATES_UPDATED = 129,
    PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_CREATED = 130,
    PXR_TYPE_SPATIAL_MESH_BOUNDING_BOX = 132,
    PXR_TYPE_SPATIAL_MESH_BOUNDING_SPHERE = 133,
    PXR_TYPE_SPATIAL_MESH_BOUNDING_FRUSTUM = 134,
    PXR_TYPE_SPATIAL_MESH_SETTING_INFO  = 135,
    PXR_TYPE_SPATIAL_MESH_SETTINGS = 136,
    PXR_TYPE_SPATIAL_FINAL_MESH_INFO = 137,
    PXR_TYPE_EVENT_DATA_SPATIAL_MESH_COMPONENT_LOAD_COMPLETE = 138,
    PXR_TYPE_EVENT_DATA_SPATIAL_MESH_CHANGED = 139,
    PXR_TYPE_EVENT_DATA_SPATIAL_MESH_INFO_LOAD_COMPLETE = 140,
    PXR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_DETECTED = 141,
    PXR_TYPE_EVENT_DATA_SPATIAL_MAP_SIZE_LIMITED = 142,
    PXR_TYPE_EVENT_MR_MAX = 200,

    PXR_TYPE_EVENT_MOTION_TRACKER_KEY_EVENT = 201,
    PXR_TYPE_EVENT_EXT_DEV_CONNECT_STATE_EVENT = 202,
    PXR_TYPE_EVENT_EXT_DEV_BATTERY_STATE_EVENT = 203,
    PXR_TYPE_EVENT_MOTION_TRACKING_MODE_CHANGED_EVENT = 204,
    PXR_TYPE_EVENT_EXT_DEV_PASS_DATA_EVENT = 205,
    /*
     * mr sdk 3.0
     */
    PXR_TYPE_SPATIAL_MESH_PROVIDER_CREATE_INFO = 221,
    PXR_TYPE_SPATIAL_ANCHOR_PROVIDER_CREATE_INFO = 222,
    PXR_TYPE_PLANE_DETECTION_PROVIDER_CREATE_INFO = 223,
    PXR_TYPE_SCENE_CAPTURE_PROVIDER_CREATE_INFO = 224,
    PXR_TYPE_SENSE_DATA_PROVIDER_START_COMPLETION = 225,
    PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_FILTER = 226,
    PXR_TYPE_SPATIAL_ENTITY_UUID_FILTER = 227,
    PXR_TYPE_SPATIAL_ENTITY_LOCATION_GET_INFO = 228,
    PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_GET_INFO = 229,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_2D_GET_INFO = 230,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_3D_GET_INFO = 231,
    PXR_TYPE_SPATIAL_ENTITY_POLYGON_GET_INFO = 232,
    PXR_TYPE_SPATIAL_ENTITY_TRIANGLE_MESH_GET_INFO = 233,
    PXR_TYPE_SPATIAL_ENTITY_LOCATION_INFO = 234,
    PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_INFO = 235,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_2D_INFO = 236,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_3D_INFO = 237,
    PXR_TYPE_SPATIAL_ENTITY_POLYGON_INFO = 238,
    PXR_TYPE_SPATIAL_ENTITY_TRIANGLE_MESH_INFO = 239,
    PXR_TYPE_SPATIAL_ANCHOR_SHARE_INFO = 240,
    PXR_TYPE_SPATIAL_ANCHOR_SHARE_COMPLETION = 241,
    PXR_TYPE_SPATIAL_ANCHOR_DOWNLOAD_INFO = 242,
    PXR_TYPE_SPATIAL_ANCHOR_DOWNLOAD_COMPLETION = 243,
    PXR_TYPE_SPATIAL_ENTITY_ANCHOR_RETRIEVE_INFO = 244,
    PXR_TYPE_ANCHOR_LOCATE_INFO = 245,
    PXR_TYPE_SPATIAL_ANCHOR_CREATE_INFO = 246,
    PXR_TYPE_SPATIAL_ANCHOR_CREATE_COMPLETION = 247,
    PXR_TYPE_SPATIAL_ANCHOR_PERSIST_INFO = 248,
    PXR_TYPE_SPATIAL_ANCHOR_PERSIST_COMPLETION = 249,
    PXR_TYPE_SPATIAL_ANCHOR_UNPERSIST_INFO = 250,
    PXR_TYPE_SPATIAL_ANCHOR_UNPERSIST_COMPLETION = 251,
    PXR_TYPE_AUTO_SCENE_CAPTURE_PROVIDER_CREATE_INFO = 252,
    PXR_TYPE_SEMI_AUTO_SCENE_CAPTURE_PROVIDER_CREATE_INFO = 253,
    PXR_TYPE_SENSE_DATA_QUERY_INFO = 254,
    PXR_TYPE_SENSE_DATA_QUERY_COMPLETION = 255,
    PXR_TYPE_QUERIED_SENSE_DATA = 256,
    PXR_TYPE_QUERIED_SENSE_DATA_GET_INFO = 257,
    PXR_TYPE_SENSE_DATA_PROVIDER_START_INFO = 258,
    PXR_TYPE_SCENE_CAPTURE_START_COMPLETION = 262,
    PXR_TYPE_SCENE_DATA_AUTO_SCENE_CAPTURE_RESULT = 263,
    PXR_TYPE_SPATIAL_ENTITY_AUTO_SCENE_CAPTURE_RESULT_GET_INFO = 264,
    PXR_TYPE_SPATIAL_ENTITY_SEMI_AUTO_SCENE_CAPTURE_RESULT_INFO  = 265,
    PXR_TYPE_SPATIAL_ENTITY_SEMI_AUTO_SCENE_CAPTURE_RESULT_GET_INFO = 266,
    PXR_TYPE_SPATIAL_ENTITY_COMPONENT_SET_INFO = 267,
    PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_COMPONENT_INFO = 268,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_2D_COMPONENT_INFO = 269,
    PXR_TYPE_SPATIAL_ENTITY_POLYGON_COMPONENT_INFO = 270,
    PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_3D_COMPONENT_INFO = 271,
    PXR_TYPE_SENSE_UNPERSIST_ANCHOR_BY_UUID_COMPLETION = 272,
    PXR_TYPE_EVENT_DATA_SPATIAL_MAP_MEM_LIMITED = 273,

    /*
     * future ext
     */
    PXR_TYPE_FUTURE_POLL_INFO_EXT = 300,
    PXR_TYPE_FUTURE_POLL_RESULT_EXT = 301,

    /*
     * mr sdk 3.0 event
     */
    PXR_TYPE_EVENT_AUTO_ROOM_CAPTURE_UPDATED = 131, // This value is shared between 2.0 and 3.0
    PXR_TYPE_EVENT_DATA_SENSE_DATA_UPDATED = 400,
    PXR_TYPE_EVENT_DATA_SENSE_DATA_PROVIDER_STATE_CHANGED = 401,

    /*
     * common type
     */
    PXR_TYPE_SPACE_LOCATION = 1000,
    PXR_TYPE_SPACE_VELOCITY = 1001,
    PXR_TYPE_EVENT_VST_DISPLAY_STATUS_CHANGED = 1002,
} PxrStructureType;

typedef enum
{
    PXR_EVENT_LEVEL_LOW = 0,
    PXR_EVENT_LEVEL_MID,
    PXR_EVENT_LEVEL_HIGH
} PxrEventLevel;

typedef enum
{
    PXR_SESSION_STATE_UNKNOWN = 0,
    PXR_SESSION_STATE_IDLE = 1,
    PXR_SESSION_STATE_READY = 2,
    PXR_SESSION_STATE_SYNCHRONIZED = 3,
    PXR_SESSION_STATE_VISIBLE = 4,
    PXR_SESSION_STATE_FOCUSED = 5,
    PXR_SESSION_STATE_STOPPING = 6,
    PXR_SESSION_STATE_LOSS_PENDING = 7,
    PXR_SESSION_STATE_EXITING = 8,
    PXR_SESSION_STATE_MAX_ENUM = 0x7FFFFFFF
} PxrSessionState;

typedef enum
{
    PXR_PERF_SETTINGS_DOMAIN_CPU = 1,
    PXR_PERF_SETTINGS_DOMAIN_GPU = 2,
    PXR_PERF_SETTINGS_DOMAIN_MAX_ENUM = 0x7FFFFFFF
} PxrPerfSettingsDomain;

typedef enum
{
    PXR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING = 1,
    PXR_PERF_SETTINGS_SUB_DOMAIN_RENDERING = 2,
    PXR_PERF_SETTINGS_SUB_DOMAIN_THERMAL = 3,
    PXR_PERF_SETTINGS_SUB_DOMAIN_MAX_ENUM = 0x7FFFFFFF
} PxrPerfSettingsSubDomain;

typedef enum
{
    PXR_PERF_SETTINGS_NOTIF_LEVEL_LOW = 0,
    PXR_PERF_SETTINGS_NOTIF_LEVEL_MID = 25,
    PXR_PERF_SETTINGS_NOTIF_LEVEL_HIGH = 75,
    PXR_PERF_SETTINGS_NOTIFICATION_LEVEL_MAX_ENUM = 0x7FFFFFFF
} PxrPerfSettingsNotificationLevel;

typedef enum
{
    PXR_PERF_SETTINGS_CPU = 1,
    PXR_PERF_SETTINGS_GPU = 2,
    PXR_PERF_SETTINGS_MAX_ENUM = 0x7FFFFFFF
} PxrPerfSettings;

typedef enum
{
    PXR_PERF_SETTINGS_LEVEL_POWER_SAVINGS = 0,
    PXR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW = 25,
    PXR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH = 50,
    PXR_PERF_SETTINGS_LEVEL_BOOST = 75,
    PXR_PERF_SETTINGS_LEVEL_MAX_ENUM = 0x7FFFFFFF
} PxrPerfSettingsLevel;
/*
 device post event type pico add new device interface
*/
typedef enum
{
    PXR_DEVICE_CONNECTCHANGED = 0,
    PXR_DEVICE_MAIN_CHANGED = 1,
    PXR_DEVICE_VERSION = 2,
    PXR_DEVICE_SN = 3,
    PXR_DEVICE_BIND_STATUS = 4,
    PXR_STATION_STATUS = 5,
    PXR_DEVICE_IOBUSY = 6,
    PXR_DEVICE_OTASTAUS = 7,
    PXR_DEVICE_ID = 8,
    PXR_DEVICE_OTASATAION_PROGRESS = 9,
    PXR_DEVICE_OTASATAION_CODE = 10,
    PXR_DEVICE_OTACONTROLLER_PROGRESS = 11,
    PXR_DEVICE_OTACONTROLLER_CODE = 12,
    PXR_DEVICE_OTA_SUCCESS = 13,
    PXR_DEVICE_BLEMAC = 14,
    PXR_DEVICE_HANDNESS_CHANGED = 15,
    PXR_DEVICE_CHANNEL = 16,
    PXR_DEVICE_LOSSRATE = 17,
    PXR_DEVICE_THREAD_STARTED = 18,
    PXR_DEVICE_MENUPRESSED_STATE = 19,
    PXR_DEVICE_HANDTRACKING_SETTING = 20,
    PXR_DEVICE_INPUTDEVICE_CHANGED = 21,
    PXR_DEVICE_SYSTEMGESTURE_STATE = 22,
	PXR_DEVICE_FITNESSBAND_STATE = 23,
	PXR_DEVICE_FITNESSBAND_BATTERY = 24,
	PXR_DEVICE_BODYTRACKING_STATE_ERROR_CODE =25,
	PXR_DEVICE_BODYTRACKING_ACTION = 26
}PxrDeviceEventType;

typedef enum
{
    PXR_EYE_LEVEL = 0,
    PXR_FLOOR_LEVEL,
    PXR_STAGE_LEVEL
} PxrTrackingOrigin;

typedef enum
{
    PXR_CONTROLLER_LEFT = 0,
    PXR_CONTROLLER_RIGHT,
    PXR_CONTROLLER_COUNT
} PxrControllerHandness;

typedef enum
{
    PXR_HMD_3DOF = 0,
    PXR_HMD_6DOF
} PxrHmdDof;


typedef enum
{
    PXR_OVERLAY = 0,
    PXR_UNDERLAY
} PxrLayerType;

typedef enum
{
    PXR_SET_SEETHROUGH_VISIBLE = 0,
    PXR_SET_GUARDIANSYSTEM_DISABLE,
    PXR_RESUME_GUARDIANSYSTEM_FOR_STS,
    PXR_PAUSE_GUARDIANSYSTEM_FOR_STS,
    PXR_SHUTDOWN_SDK_GUARDIANSYSTEM,
    PXR_GET_CAMERA_DATA_EXT,
    PXR_START_SDK_BOUNDARY,
    PXR_SET_CONTROLLER_POSITION,
    PXR_START_CAMERA_PREVIEW,
    PXR_GET_ROOM_MODE_STATE,
    PXR_DISABLE_BOUNDARY,
    PXR_SET_MONO_MODE,
    PXR_GET_BOUNDARY_CONFIGURED,
    PXR_GET_BOUNDARY_ENABLED,
    PXR_SET_BOUNDARY_VISIBLE,
    PXR_SET_SEETHROUGH_BACKGROUND,
    PXR_GET_BOUNDARY_VISIBLE,
    PXR_GET_DIALOG_STATE,
    PXR_SET_SENSORLOST_CUSTOM_MODE,
    PXR_SET_SENSORLOST_CM_ST,
    PXR_GET_BOUNDARY_GEOMETRY_VEX_COUNT,

    PXR_RESET_TRACKING_HARD = 1000,
    PXR_GET_TRACKING_STATE = 1001,
    PXR_SET_ORIGIN_OF_LARGE_SPACE = 1002,
    PXR_SET_ORIGIN_OF_LARGE_SPACE_NOSAVE = 1003,
} PxrFuncitonName;

typedef enum
{
    PXR_BOUNDARY_TEST_NODE_LEFT_HAND = 0,
    PXR_BOUNDARY_TEST_NODE_RIGHT_HAND = 1,
    PXR_BOUNDARY_TEST_NODE_HEAD = 2
}PxrBoundaryTestNode;

typedef enum PxrBlendFactor_ {
    PXR_BLEND_FACTOR_ZERO = 0,
    PXR_BLEND_FACTOR_ONE = 1,
    PXR_BLEND_FACTOR_SRC_ALPHA = 2,
    PXR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 3,
    PXR_BLEND_FACTOR_DST_ALPHA = 4,
    PXR_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 5,
    PXR_BLEND_FACTOR_MAX_ENUM = 0xFFFFFFFF
} PxrBlendFactor;

enum PxrGazeType {
	NEVER = 0,
	DURING_MOTION = 1,
	ALWAYS = 2,
};

enum PxrArmmodelType {
	CONTROLLER = 0,
	WRIST = 1,
	ELBOW = 2,
	SHOULDER = 3,
};


//add mr persistence enum definition
typedef enum PxrSpatialInstanceType
{
    PXR_SPATIAL_ENTITY_TYPE_NOT_DEFINED = 0,
    PXR_SPATIAL_ENTITY_TYPE_SPATIAL_ANCHOR = 1,
    PXR_SPATIAL_ENTITY_TYPE_SPATIAL_MODEL = 2
} PxrSpatialInstanceType;

// The tag can be set to a spatial instance
typedef enum PxrSpatialInstanceTag
{
    PXR_SPATIAL_ENTITY_TAG_NO_TAG = 0,
    PXR_SPATIAL_ENTITY_TAG_FLOOR = 1,
    PXR_SPATIAL_ENTITY_TAG_WALL = 2,
    PXR_SPATIAL_ENTITY_TAG_CELLING = 3,
    PXR_SPATIAL_ENTITY_TAG_COUCH = 4,
    PXR_SPATIAL_ENTITY_TAG_TABLE = 5,
} PxrSpatialInstanceTag;

// The reference frame in which the pose is calculated,
// Currently Local and Global are supported.
typedef enum PxrReferenceType
{
    PXR_REFERENCE_TYPE_NOT_DEFINED = 0,
    PXR_REFERENCE_TYPE_LOCAL = 1,
    PXR_REFERENCE_TYPE_GLOBAL = 2
} PxrReferenceType;

// Storage location to be used to store, load, erase, and query spatial instances from
typedef enum PxrSpatialPersistenceLocation
{
    PXR_SPATIAL_PERSISTENCE_LOCATION_NOT_DEFINED = 0,
    PXR_SPATIAL_PERSISTENCE_LOCATION_LOCAL = 1, // local device storage
    PXR_SPATIAL_PERSISTENCE_LOCATION_REMOTE = 2, // remote storage
} PxrSpatialPersistenceLocation;

// Persistence mode, only one mode is supported and may be more mode in future.
typedef enum PxrSpatialPersistenceMode
{
    PXR_SPATIAL_PERSISTENCE_MODE_NOT_DEFINED = 0,
    PXR_SPATIAL_PERSISTENCE_MODE_DEFAULT = 1, // only this mode is supported now.
} PxrSpatialPersistenceMode;

// Property type a spatial instance may has.
typedef enum PxrSpatialInstancePropertyType
{
    PXR_SPATIAL_INSTANCE_PROPERTY_TYPE_NOT_DEFINED = 0,
    PXR_SPATIAL_INSTANCE_PROPERTY_TYPE_SHARABLE = 1, // To indicate if the spatial entity can be shared with other app.
} PxrSpatialInstancePropertyType;

// PxrSpatialPersistenceResult
typedef enum PxrSpatialPersistenceResult
{
    PXR_SPATIAL_PERSISTENCE_RESULT_SUCCESS = 0,
    PXR_SPATIAL_PERSISTENCE_RESULT_TIMEOUT_EXPIRED = 1,
    PXR_SPATIAL_PERSISTENCE_RESULT_ERROR_VALIDATION_FAILURE = -1,
    PXR_SPATIAL_PERSISTENCE_RESULT_ERROR_RUNTIME_FAILURE = -2,
} PxrSpatialPersistenceResult;

typedef enum PxrStartMrOptionFlag
{
    // These are a bit mask of start MR Mode options
    REQUIRE_ROOM_SCENE      = (1 << 0),
    NOT_DEFINED              = (1 << 1),
}PxrStartMrOptionFlag;

typedef enum PxrLayerImageTypes_ {
    PXR_IMAGE_TYPE_DEFAULT,
    PXR_LAYER_IMAGE_TYPE_MOTION_VECTOR,
    PXR_LAYER_IMAGE_TYPE_DEPTH,
    PXR_LAYER_IMAGE_TYPE_FDM,
} PxrLayerImageTypes;

typedef enum PxrETResult {
  PXR_ET_RESULT_SUCCESS,
  PXR_ET_RESULT_SWITCH_NOT_ENABLE = -1,
  PXR_ET_RESULT_PERMISION_NOT_ENABLE = -2,
} PxrETResult;

typedef enum PxrFTResult {
  PXR_FT_RESULT_SUCCESS,
  PXR_FT_RESULT_SWITCH_NOT_ENABLE = -1,
  PXR_FT_RESULT_FT_PERMISION_NOT_ENABLE = -2,
  PXR_FT_RESULT_MIC_PERMISION_NOT_ENABLE = -4,
} PxrFTResult;


//mr v2.0
typedef enum PxrSpatialTrackingState {
    PXR_SPATIAL_TRACKING_STATE_INVALID_ = 0,
    PXR_SPATIAL_TRACKING_STATE_VALID_ = 1,
    PXR_SPATIAL_TRACKING_STATE_LIMITED_ = 2,
} PxrSpatialTrackingState ;
typedef enum PxrSpatialTrackingStateMessage {
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_UNKNOWN = 0, //default value
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_INTERNAL_ERROR = 1, // INVALID, system error or alg error
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_RELOCATING = 2, // INVALID, 6dof lost, app needs clear anchors in memory
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_RELOCATED = 3, // VALID, 6dof recovered/mr map ready, app can load anchors
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATING = 100,//INVALID, usually in resume
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATED = 101,//VALID, successfully located.
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATING_FAILED = 102,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATING_FAILED_INVALID_MAP = 103,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATING_FAILED_NO_MAP = 104,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATE_STOPPING = 105,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATE_STOP_FAILED = 106,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_LOCATE_STOPPED = 107,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_CREATING = 108,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_CREATE_FAILED = 109,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_CREATED = 110,//LIMITED
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_SAVING = 111,//LIMITED
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_SAVE_FAILED = 112,//LIMITED
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_SAVE_FAILED_LOW_QUALITY = 113,//LIMITED
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_SAVE_FAILED_INSUFFICENT_DISK_SPACE = 114,//LIMITED
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MAP_SAVED = 115,//VALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MR_ENGINE_STARTED = 116,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MR_ENGINE_STOPPED = 117,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MR_ENGINE_DESTROYED = 118,//INVALID
    PXR_SPATIAL_TRACKING_STATE_MESSAGE_MR_MAP_LOSS = 119,//INVALID
    PXR_SPATIAL_TRACKING_STATE_CHANGE_REASON_MAX_ENUM= 0x7FFFFFFF
} PxrSpatialTrackingStateMessage ;
typedef enum PxrResult {
    PXR_SUCCESS = 0,
    PXR_TIMEOUT_EXPIRED = 1,
    //PXR_SPATIAL_MESH_DATA_NO_UPDATE = 1000395100,
    //PXR_ERROR_SPATIAL_MESH_VOLUMES_UPDATE_FAILED = -1000395001,
    PXR_ERROR_VALIDATION_FAILURE = -1,
    PXR_ERROR_RUNTIME_FAILURE = -2,
    PXR_ERROR_OUT_OF_MEMORY = -3,
    PXR_ERROR_API_VERSION_UNSUPPORTED = -4,
    PXR_ERROR_INITIALIZATION_FAILED = -6,
    PXR_ERROR_FUNCTION_UNSUPPORTED = -7,
    PXR_ERROR_FEATURE_UNSUPPORTED = -8,
    //PXR_ERROR_EXTENSION_NOT_PRESENT = -9,
    PXR_ERROR_LIMIT_REACHED = -10,
    PXR_ERROR_SIZE_INSUFFICIENT = -11,
    PXR_ERROR_HANDLE_INVALID = -12,

    /*
    PXR_ERROR_INSTANCE_LOST = -13,
    PXR_ERROR_SESSION_RUNNING = -14,
    PXR_ERROR_SESSION_NOT_RUNNING = -16,
    PXR_ERROR_SESSION_LOST = -17,
    PXR_ERROR_SYSTEM_INVALID = -18,
    PXR_ERROR_PATH_INVALID = -19,
    PXR_ERROR_PATH_COUNT_EXCEEDED = -20,
    PXR_ERROR_PATH_FORMAT_INVALID = -21,
    PXR_ERROR_PATH_UNSUPPORTED = -22,
    PXR_ERROR_LAYER_INVALID = -23,
    PXR_ERROR_LAYER_LIMIT_EXCEEDED = -24,
    PXR_ERROR_SWAPCHAIN_RECT_INVALID = -25,
    PXR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED = -26,
    PXR_ERROR_ACTION_TYPE_MISMATCH = -27,
    PXR_ERROR_SESSION_NOT_READY = -28,
    PXR_ERROR_SESSION_NOT_STOPPING = -29,
    PXR_ERROR_TIME_INVALID = -30,
    PXR_ERROR_REFERENCE_SPACE_UNSUPPORTED = -31,
    PXR_ERROR_FILE_ACCESS_ERROR = -32,
    PXR_ERROR_FILE_CONTENTS_INVALID = -33,
    PXR_ERROR_FORM_FACTOR_UNSUPPORTED = -34,
    PXR_ERROR_FORM_FACTOR_UNAVAILABLE = -35,
    PXR_ERROR_API_LAYER_NOT_PRESENT = -36,
    PXR_ERROR_CALL_ORDER_INVALID = -37,
    PXR__ERROR_GRAPHICS_DEVICE_INVALID = -38,
    */
    PXR_ERROR_POSE_INVALID = -39,
    /*
    PXR_ERROR_INDEX_OUT_OF_RANGE = -40,
    PXR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED = -41,
    PXR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED = -42,
    PXR_ERROR_NAME_DUPLICATED = -44,
    PXR_ERROR_NAME_INVALID = -45,
    PXR_ERROR_ACTIONSET_NOT_ATTACHED = -46,
    PXR_ERROR_ACTIONSETS_ALREADY_ATTACHED = -47,
    PXR_ERROR_LOCALIZED_NAME_DUPLICATED = -48,
    PXR_ERROR_LOCALIZED_NAME_INVALID = -49,
    PXR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING = -50,
    PXR_ERROR_RUNTIME_UNAVAILABLE = -51,
    PXR_RESULT_MAX_ENUM = 0x7FFFFFFF
    */
    PXR_ERROR_SPATIAL_LOCALIZATION_RUNNING = -1000,
    PXR_ERROR_SPATIAL_LOCALIZATION_NOT_RUNNING = -1001,
    PXR_ERROR_SPATIAL_MAP_CREATED = -1002,
    PXR_ERROR_SPATIAL_MAP_NOT_CREATED = -1003,
    PXR_ERROR_SPATIAL_SENSING_SERVICE_UNAVAILABLE =-1005,
    PXR_ERROR_COMPONENT_NOT_SUPPORTED = -501,
    PXR_ERROR_COMPONENT_CONFLICT = -502,
    PXR_ERROR_COMPONENT_NOT_ADDED = -503,
    PXR_ERROR_COMPONENT_ADDED = -504,
    PXR_ERROR_ANCHOR_ENTITY_NOT_FOUND = -505,
    PXR_ERROR_TRACKING_STATE_INVALID = -506,
    PXR_ERROR_SPACE_LOCATING = -507,

    PXR_ERROR_ANCHOR_SHARING_NETWORK_TIMEOUT = -601, 
    PXR_ERROR_ANCHOR_SHARING_AUTHENTICATION_FAILURE = -602,
    PXR_ERROR_ANCHOR_SHARING_NETWORK_FAILURE = -603,
    PXR_ERROR_ANCHOR_SHARING_LOCALIZATION_FAIL = -604,
    PXR_ERROR_ANCHOR_SHARING_MAP_INSUFFICIENT = -605,

    PXR_ERROR_PERMISSION_INSUFFICIENT = -1000710000,
} PxrResult;

typedef enum PxrAnchorComponentType {
    PXR_ANCHOR_COMPONENT_TYPE_POSE_ = 0, 
    PXR_ANCHOR_COMPONENT_TYPE_PERSISTENCE_ = 1, 
    PXR_ANCHOR_COMPONENT_TYPE_SCENE_LABEL_ = 2, 
    PXR_ANCHOR_COMPONENT_TYPE_PLANE_ = 3, 
    PXR_ANCHOR_COMPONENT_TYPE_BOX_ = 4, 
    PXR_ANCHOR_COMPONENT_TYPE_MAX_ENUM_ = 0x7FFFFFFF
} PxrAnchorComponentType ;

typedef enum PxrSceneLabel {
    PXR_SCENE_LABEL_UNKNOWN_ = 0,
    PXR_SCENE_LABEL_FLOOR_ = 1,
    PXR_SCENE_LABEL_CEILING_ = 2,
    PXR_SCENE_LABEL_WALL_ = 3,
    PXR_SCENE_LABEL_DOOR_ = 4,
    PXR_SCENE_LABEL_WINDOW_ = 5,
    PXR_SCENE_LABEL_OPENING_ = 6,
    PXR_SCENE_LABEL_TABLE_ = 7,
    PXR_SCENE_LABEL_SOFA_ = 8,
    //new scene label
    PXR_SCENE_LABEL_CHAIR_ = 9,
    PXR_SCENE_LABEL_HUMAN_ = 10,
    PXR_SCENE_LABEL_BEAM_ = 11,
    PXR_SCENE_LABEL_COLUMN_ = 12,
    PXR_SCENE_LABEL_CURTAIN_ = 13,
    PXR_SCENE_LABEL_CABINET_ = 14,
    PXR_SCENE_LABEL_BED_ = 15,
    PXR_SCENE_LABEL_PLANT_ = 16,
    PXR_SCENE_LABEL_SCREEN_ = 17,
    PXR_SCENE_LABEL_VIRTUAL_WALL_ = 18,

} PxrSceneLabel ;

typedef enum PxrPersistLocation {
    PXR_PERSIST_LOCATION_LOCAL = 1,
    PXR_PERSIST_LOCATION_REMOTE = 2,
} PxrPersistLocation ;



typedef enum
{
    PXR_PASSTHROUGH_CONTRAST = 0,
    PXR_PASSTHROUGH_SATURATION = 1,
    PXR_PASSTHROUGH_BRIGHTNESS = 2,
    PXR_PASSTHROUGH_COLORTEMP = 3,
} PxrLayerEffect;

//typedef enum PxrRoomCaptureCandidateType {
//    PXR_ROOM_CAPTURE_TYPE_UNKNOWN = 0,
//    PXR_ROOM_CAPTURE_TYPE_CORNER_POINT,
//    PXR_ROOM_CAPTURE_TYPE_CORNER_LINE,
//} PxrRoomCaptureCandidateType ;

typedef enum PxrAdaptiveResolutionPowerSetting {
    PXR_ADAPTIVE_RESOLUTION_HIGH_QUALITY, // performance factor = 0.9
    PXR_ADAPTIVE_RESOLUTION_BALANCED, // performance factor = 0.8
    PXR_ADAPTIVE_RESOLUTION_BATTERY_SAVING // performance factor = 0.7
} PxrAdaptiveResolutionPowerSetting;

typedef enum PxrPersistenceLocation {
    PXR_PERSISTENCE_LOCATION_LOCAL = 0,
} PxrPersistenceLocation;

typedef enum PxrSpatialEntityComponentType{
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_LOCATION = 0,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_SEMANTIC = 1,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_BOUNDING_BOX_2D = 2,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_POLYGON = 3,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_BOUNDING_BOX_3D = 4,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_TRIANGLE_MESH = 5,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_AUTO_SCENE_CAPTURE_RESULT = 1000,
    PXR_SPATIAL_ENTITY_COMPONENT_TYPE_SEMI_AUTO_SCENE_CAPTURE_RESULT = 1001,
} PxrSpatialEntityComponentType;

typedef enum PxrSemiAutoRoomCaptureCandidateType {
    PXR_ROOM_CAPTURE_TYPE_UNKNOWN  = 0,
    PXR_ROOM_CAPTURE_TYPE_CORNER_POINT,
    PXR_ROOM_CAPTURE_TYPE_CORNER_LINE,
} PxrSemiAutoRoomCaptureCandidateType;

// sdk3.0 auto scene capture

typedef enum PxrSpatialSceneCaptureStatus {
    PXR_AUTO_SPATIAL_SCENE_CAPTURE_STATUS_NOT_DEFINED = 0,
    PXR_AUTO_SPATIAL_SCENE_CAPTURE_STATUS_NEW_CAPTURE_RESULT = 1,
    PXR_AUTO_SPATIAL_SCENE_CAPTURE_STATUS_OUT_OF_CAPTURE_ZONE = 2,
    PXR_AUTO_SPATIAL_SCENE_CAPTURE_STATUS_ERROR_MESSAGE = 3,
} PxrSpatialSceneCaptureStatus;

typedef PxrSpatialSceneCaptureStatus PxrSceneCaptureStatus;

enum PxrRecenterTypes
{
	RecenterNone = 0,
	RecenterOrientation = 0x1,
	RecenterPosition = 0x2,
	RecenterOrientationAndPosition = 0x3
};

typedef uint64_t PxrTrackingModeFlags;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_ROTATION_BIT = 0x00000001;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_POSITION_BIT = 0x00000002;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_EYE_BIT = 0x00000004;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_FACE_BIT = 0x00000008;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_FACE_LIBSYNC = 0x2000;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_VCMOTOR_BIT = 0x00000010;
static const PxrTrackingModeFlags PXR_TRACKING_MODE_HAND_BIT = 0x00000020;

typedef struct PxrVulkanBinding_ {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t queueFamilyIndex;
    uint32_t queueIndex;
    const void* next;
} PxrVulkanBinding;

typedef struct PxrLayerParam_ {
    int                layerId;
    PxrLayerShape      layerShape;
    PxrLayerType       layerType;
    PxrLayerLayout     layerLayout;
    uint64_t           format;
    uint32_t           width;
    uint32_t           height;
    uint32_t           sampleCount;
    uint32_t           faceCount;
    uint32_t           arraySize;
    uint32_t           mipmapCount;
    uint32_t           layerFlags;
    uint32_t           externalImageCount;
    uint64_t*          externalImages[2];
} PxrLayerParam;

typedef struct PxrQuaternionf_ {
    float    x;
    float    y;
    float    z;
    float    w;
} PxrQuaternionf;

typedef struct PxrVector2f_ {
    float    x;
    float    y;
} PxrVector2f;

typedef struct PxrVector3f_ {
    float    x;
    float    y;
    float    z;
} PxrVector3f;

typedef struct PxrVector4f_ {
    float    x;
    float    y;
    float    z;
    float    w;
}PxrVector4f;

typedef struct PxrRecti_ {
    int x;
    int y;
    int width;
    int height;
} PxrRecti;

typedef struct PxrPosef_ {
    PxrQuaternionf    orientation;
    PxrVector3f       position;
} PxrPosef;

typedef struct PxrSensorState_ {
    int status;
    PxrPosef pose;
    PxrVector3f angularVelocity;
    PxrVector3f linearVelocity;
    PxrVector3f angularAcceleration;
    PxrVector3f linearAcceleration;
    uint64_t poseTimeStampNs;
} PxrSensorState;

typedef struct PxrSensorState2_ {
    int         status;
    PxrPosef    pose;
    PxrPosef    globalPose;
    PxrVector3f angularVelocity;
    PxrVector3f linearVelocity;
    PxrVector3f angularAcceleration;
    PxrVector3f linearAcceleration;
    uint64_t    poseTimeStampNs;
} PxrSensorState2;

enum pxrEyePoseStatus
{
	kGazePointValid                     = (1 << 0),
	kGazeVectorValid                    = (1 << 1),
	kEyeOpennessValid                   = (1 << 2),
	kEyePupilDilationValid              = (1 << 3),
	kEyePositionGuideValid              = (1 << 4),
	kEyePupilPositionValid              = (1 << 5),
	kEyeConvergenceDistanceValid        = (1 << 6),
	kEyeGazePointValid                  = (1 << 7),
	kEyeGazeVectorValid                 = (1 << 8),
	kPupilDistanceValid                 = (1 << 9),
	kConvergenceDistanceValid           = (1 << 10),
	kPupilDiameterValid                 = (1 << 11),
};

typedef struct PxrEyeTrackingData_ {
    int32_t    leftEyePoseStatus;          //!< Bit field (pvrEyePoseStatus) indicating left eye pose status
    int32_t    rightEyePoseStatus;         //!< Bit field (pvrEyePoseStatus) indicating right eye pose status
    int32_t    combinedEyePoseStatus;      //!< Bit field (pvrEyePoseStatus) indicating combined eye pose status

    float      leftEyeGazePoint[3];        //!< Left Eye Gaze Point
    float      rightEyeGazePoint[3];       //!< Right Eye Gaze Point
    float      combinedEyeGazePoint[3];    //!< Combined Eye Gaze Point (HMD center-eye point)

    float      leftEyeGazeVector[3];       //!< Left Eye Gaze Point
    float      rightEyeGazeVector[3];      //!< Right Eye Gaze Point
    float      combinedEyeGazeVector[3];   //!< Comnbined Eye Gaze Vector (HMD center-eye point)

    float      leftEyeOpenness;            //!< Left eye value between 0.0 and 1.0 where 1.0 means fully open and 0.0 closed.
    float      rightEyeOpenness;           //!< Right eye value between 0.0 and 1.0 where 1.0 means fully open and 0.0 closed.

    float      leftEyePupilDilation;       //!< Left eye value in millimeters indicating the pupil dilation
    float      rightEyePupilDilation;      //!< Right eye value in millimeters indicating the pupil dilation

    float      leftEyePositionGuide[3];    //!< Position of the inner corner of the left eye in meters from the HMD center-eye coordinate system's origin.
    float      rightEyePositionGuide[3];   //!< Position of the inner corner of the right eye in meters from the HMD center-eye coordinate system's origin.
    float      foveatedGazeDirection[3];   //!< Position of the gaze direction in meters from the HMD center-eye coordinate system's origin.
    int32_t    foveatedGazeTrackingState;  //!< The current state of the foveatedGazeDirection signal.
} PxrEyeTrackingData;

typedef struct PxrEyePupilInfo{
    float leftEyePupilDiameter;
    float rightEyePupilDiameter;
    float leftEyePupilPosition[2];
    float rightEyePupilPosition[2];
}PxrEyePupilInfo;

enum BlendShapeIndex {
    EyeLookDown_L = 0,
    NoseSneer_L = 1,
    EyeLookIn_L = 2,
    BrowInnerUp = 3,
    BrowDown_L = 25,
    MouthClose = 5,
    MouthLowerDown_R = 6,
    JawOpen = 7,
    MouthLowerDown_L = 9,
    MouthFunnel = 10,
    EyeLookIn_R = 11,
    EyeLookDown_R = 12,
    NoseSneer_R = 13,
    MouthRollUpper = 14,
    JawRight = 15,
    MouthDimple_L = 16,
    MouthRollLower = 17,
    MouthSmile_L = 18,
    MouthPress_L = 19,
    MouthSmile_R = 20,
    MouthPress_R = 21,
    MouthDimple_R = 22,
    MouthLeft = 23,
    EyeSquint_R = 41,
    EyeSquint_L = 4,
    MouthFrown_L = 26,
    EyeBlink_L = 27,
    CheekSquint_L = 28,
    BrowOuterUp_L = 29,
    EyeLookUp_L = 30,
    JawLeft = 31,
    MouthStretch_L = 32,
    MouthStretch_R = 33,
    MouthPucker = 34,
    EyeLookUp_R = 35,
    BrowOuterUp_R = 36,
    CheekSquint_R = 37,
    EyeBlink_R = 38,
    MouthUpperUp_L = 39,
    MouthFrown_R = 40,
    BrowDown_R = 24,
    JawForward = 42,
    MouthUpperUp_R = 43,
    CheekPuff = 44,
    EyeLookOut_L = 45,
    EyeLookOut_R = 46,
    EyeWide_R = 47,
    EyeWide_L = 49,
    MouthRight = 48,
    MouthShrugLower = 8,
    MouthShrugUpper = 50,
    TongueOut = 51,
};

typedef enum {
    PXR_VST_DISPLAY_DISABLED = 0,
    PXR_VST_DISPLAY_ENABLING,
    PXR_VST_DISPLAY_ENABLED,
    PXR_VST_DISPLAY_DISABLING
} PxrVstStatus;

typedef struct PxrLayerHeader_ {
    int              layerId;
    uint32_t         layerFlags;
    float            colorScale[4];
    float            colorBias[4];
    int              compositionDepth;
    int              sensorFrameIndex;
    int              imageIndex;
    PxrPosef         headPose;
} PxrLayerHeader;

typedef struct PxrLayerBlend_ {
    PxrBlendFactor srcColor;
    PxrBlendFactor dstColor;
    PxrBlendFactor srcAlpha;
    PxrBlendFactor dstAlpha;
}PxrLayerBlend;

typedef struct PxrLayerHeader2_ {
    int              layerId;
    uint32_t         layerFlags;
    float            colorScale[4];
    float            colorBias[4];
    int              compositionDepth;
    int              sensorFrameIndex;
    int              imageIndex;
    PxrPosef         headPose;
    PxrLayerShape    layerShape;
    uint32_t         useLayerBlend;
    PxrLayerBlend    layerBlend;
    uint32_t         useImageRect;
    PxrRecti         imageRect[2];
    uint64_t         reserved[4];
} PxrLayerHeader2;

typedef struct PxrLayerProjection2_ {
    PxrLayerHeader2   header;
    float             depth;
    PxrVector4f       motionVectorScale;
    PxrVector4f       motionVectorOffset;
    PxrPosef          deltaPose;
    float             minDepth;
    float             maxDepth;
    float             nearZ;
    float             farZ;
} PxrLayerProjection2;

typedef struct PxrLayerVstMask2_ {
    PxrLayerHeader2   header;
} PxrLayerVstMask2;

typedef struct PxrLayerQuad2_ {
    PxrLayerHeader2   header;
    PxrPosef          pose[2];
    PxrVector2f       size[2];
} PxrLayerQuad2;

typedef struct PxrLayerCylinder2_ {
    PxrLayerHeader2   header;
    PxrPosef          pose[2];
    float             radius[2];
    float             centralAngle[2];
    float             height[2];
} PxrLayerCylinder2;

typedef struct PxrLayerEquirect2_ {
    PxrLayerHeader2   header;
    PxrPosef          pose[2];
    float             radius[2];
    float             centralHorizontalAngle[2];
    float             upperVerticalAngle[2];
    float             lowerVerticalAngle[2];
} PxrLayerEquirect2;

typedef struct PxrLayerCube2_ {
    PxrLayerHeader2    header;
    PxrPosef           pose[2];
} PxrLayerCube2;

typedef struct PxrLayerEAC2_ {
    PxrLayerHeader2    header;
    PxrPosef           pose[2];
    PxrVector3f        offset[2];
    PxrQuaternionf     offsetRot[2];
    uint32_t           modelType;
    float              overlapFactor;
} PxrLayerEAC2;

typedef struct PxrLayerProjection_ {
    PxrLayerHeader    header;
    float             depth;
} PxrLayerProjection;

typedef struct PxrLayerQuad_ {
    PxrLayerHeader    header;
    PxrPosef          pose;
    float             size[2];
} PxrLayerQuad;

typedef struct PxrLayerCylinder_ {
    PxrLayerHeader    header;
    PxrPosef          pose;
    float             radius;
    float             centralAngle;
    float             height;
} PxrLayerCylinder;

typedef struct PxrLayerEquirect_ {
    PxrLayerHeader2   header;
    PxrPosef          pose[2];
    float             radius[2];
    float             scaleX[2];
    float             scaleY[2];
    float             biasX[2];
    float             biasY[2];
} PxrLayerEquirect;

typedef struct PxrLayerFisheye_ {
    PxrLayerHeader2   header;
    PxrPosef          pose[2];
    float             radius[2];
    float             scaleX[2];
    float             scaleY[2];
    float             biasX[2];
    float             biasY[2];
} PxrLayerFisheye;

typedef struct PxrEventDataBaseHeader_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
} PxrEventDataBaseHeader;

typedef struct PxrEventDataBuffer_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    uint8_t                     varying[500];
} PxrEventDataBuffer;

typedef struct PxrEventDataEventsLost_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    uint32_t                    lostEventCount;
} PxrEventDataEventsLost;

typedef struct PxrEventDataVstDisplayChanged_ {
    PxrStructureType type;
    PxrEventLevel eventLevel;
    int32_t displayStatus;
} PxrEventDataVstDisplayChanged;

typedef struct PxrEventDataInstanceLossPending_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int64_t                      lossTime;
} PxrEventDataInstanceLossPending;

typedef struct PxrEventDataSessionReady_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
} PxrEventDataSessionReady;

typedef struct PxrEventDataSessionStopping_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
} PxrEventDataSessionStopping;


typedef struct PxrEventDataSessionStateChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    PxrSessionState              state;
    int64_t                      time;
} PxrEventDataSessionStateChanged;

typedef struct PXrEventDataMainSessionVisibilityChangedEXTX_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	bool                         visible;
	char    				 	 packageName[128];
} PXrEventDataMainSessionVisibilityChangedEXTX;

typedef struct PxrEventDataInteractionProfileChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
} PxrEventDataInteractionProfileChanged;

typedef struct PxrEventDataPerfSettings_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    PxrPerfSettingsDomain               domain;
    PxrPerfSettingsSubDomain            subDomain;
    PxrPerfSettingsNotificationLevel    fromLevel;
    PxrPerfSettingsNotificationLevel    toLevel;
} PxrEventDataPerfSettings;

typedef struct PxrEventDataControllerChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    PxrDeviceEventType           eventtype;
    uint8_t                         controller;
    uint8_t                         status;
    uint8_t                     varying[400];
    uint16_t                         length;
} PxrEventDataControllerChanged;

typedef struct PxrEventDataSeethroughStateChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                          state;
} PxrEventDataSeethroughStateChanged;

typedef struct PxrEventDataHardIPDStateChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    float                       ipd;
}PxrEventDataHardIPDStateChanged;

typedef struct PxrEventDataFoveationLevelChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                        level;
} PxrEventDataFoveationLevelChanged;

typedef struct PxrEventDataFrustumChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
} PxrEventDataFrustumChanged;

typedef struct PxrEventDataRenderTextureChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                         width;
    int                         height;
} PxrEventDataRenderTextureChanged;

typedef struct PxrXrEventDataTargetFrameRateChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                         frameRate;
} PxrEventDataTargetFrameRateChanged;

typedef struct PxrXrEventDataMrcStatusChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                         mrc_status;
} PxrEventDataMrcStatusChanged;

typedef struct ExternalCameraInfo {
    int32_t width;
    int32_t height;
    float fov;
} ExternalCameraInfo; 

typedef struct PxrXrEventDataRefreshRateChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    float                        refrashRate;
} PxrEventDataRefreshRateChanged;
typedef struct PxrXrEventDataSDKLoglevelChanged_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                         xrSDKLogLevel;
} PxrXrEventDataSDKLoglevelChanged;


typedef struct PxrEventDataHmdKey_ {
    PxrStructureType             type;
    PxrEventLevel                eventLevel;
    int                         code;
    int                         action;
    int                         repeat;
} PxrEventDataHmdKey;

typedef struct PxrEventHmdBatteryChanged_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	int                         value;
} PxrEventHmdBatteryChanged;

typedef struct PxrEventDataMotionTrackerKey_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	char                        trackerSN[24];
	int                         code;
	int                         action;
	int                         repeat;
	bool                        shortPress;
} PxrEventDataMotionTrackerKey;

typedef struct PxrEventDataExtDevConnectEvent_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	char trackerSN[24];
	int32_t state;

} PxrEventDataExtDevConnectEvent;

typedef struct PxrEventDataExtDevBatteryEvent_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	char trackerSN[24];
	int32_t battery;
	int32_t charger;

} PxrEventDataExtDevBatteryEvent;

typedef struct PxrEventDataExtDevPassDataEvent_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	int32_t status;

} PxrEventDataExtDevPassDataEvent;

typedef struct PxrEventDataMotionTrackingModeChangedEvent_ {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	int32_t mode;

} PxrEventDataMotionTrackingModeChangedEvent;

typedef struct PxrEndFrameInfo_ {
    PxrPosef     headPose;
    float        depth;
} PxrEndFrameInfo;

typedef struct PxrInitParamData_ {
    void* activity;
    void* vm;
    int   controllerdof;
    int   headdof;
} PxrInitParamData;

typedef struct PxrFoveationParams_ {
    float foveationGainX;
    float foveationGainY;
    float foveationArea;
    float foveationMinimum;
}PxrFoveationParams;

typedef enum {
    PXR_FOVEATION_Success        = 0,
    PXR_FOVEATION_Success_OneEye = 1,
    PXR_FOVEATION_Failure        = -1000,
    PXR_FOVEATION_InvalidData    = -1001,
    PXR_FOVEATION_InvalidParams  = -1002,
    PXR_FOVEATION_NotInitialized = -1003,
} PxrFoveationStateCode;

typedef struct PxrBoundaryTriggerInfo_ {
	bool                  isTriggering;
    float                 closestDistance;
	PxrVector3f           closestPoint;
	PxrVector3f           closestPointNormal;
	bool                  valid;
}PxrBoundaryTriggerInfo;

typedef struct PxrSeeThoughData_ {
	uint64_t              leftEyeTextureId;
    uint64_t              rightEyeTextureId;
    uint32_t              width;
    uint32_t              height;
    uint32_t              exposure;
    int64_t               startTimeOfExposure;
    bool                  valid;
}PxrSeeThoughData;

typedef struct
{
    uint32_t slot;
    uint32_t reversal;
    float amp;
}PxrVibrate_info;

typedef struct
{
	uint64_t frameseq;
	uint16_t play;
	uint16_t frequency;
	uint16_t loop;
	float gain;
} PxrPhf_params_t;

typedef struct
{
	PxrPhf_params_t params[PHF_LENGTH];
} PxrPhf_params;

typedef struct
{
    int slot;
    uint64_t buffersize;
    int sampleRate;
    int channelCounts;
    int bitrate;
    int reversal;
    int isCache;
} PxrVibrate_config;
typedef enum XrTrackingMode
{
    XR_TRACKING_MODE_ROTATION = 0x1,
    XR_TRACKING_MODE_POSITION = 0x2,
    XR_TRACKING_MODE_EYE = 0x4,
    XR_TRACKING_MODE_FACE = 0x8
}XrTrackingMode;
#define BLEND_SHAPE_NUMS 72
typedef struct {
    int64_t timestamp;
    float blendShapeWeight[BLEND_SHAPE_NUMS];
    float videoInputValid[10];
    float laughingProb;
    float emotionProb[10];
    float reserved[128];
} PxrFTInfo;
enum GetDataType {
    PXR_GET_FACE_DATA_DEFAULT = 0,
    PXR_GET_FACE_DATA = 3,   
    PXR_GET_LIP_DATA = 4,    
    PXR_GET_FACELIP_DATA = 5,
};
////////////////////////////////////////////////

typedef enum
{
	PXR_CONTROLLER_3DOF = 0,
    PXR_CONTROLLER_6DOF
} PxrControllerDof;

typedef enum
{
    PXR_CONTROLLER_BOND = 0,
    PXR_CONTROLLER_UNBOND
} PxrControllerBond;

typedef enum
{
    PXR_CONTROLLER_KEY_HOME = 0,
    PXR_CONTROLLER_KEY_AX = 1,
    PXR_CONTROLLER_KEY_BY= 2,
    PXR_CONTROLLER_KEY_BACK = 3,
    PXR_CONTROLLER_KEY_TRIGGER = 4,
    PXR_CONTROLLER_KEY_VOL_UP = 5,
    PXR_CONTROLLER_KEY_VOL_DOWN = 6,
    PXR_CONTROLLER_KEY_ROCKER = 7,
    PXR_CONTROLLER_KEY_GRIP = 8,
    PXR_CONTROLLER_KEY_TOUCHPAD = 9,
    PXR_CONTROLLER_KEY_LASTONE = 127,

    PXR_CONTROLLER_TOUCH_AX = 128,
    PXR_CONTROLLER_TOUCH_BY = 129,
    PXR_CONTROLLER_TOUCH_ROCKER = 130,
    PXR_CONTROLLER_TOUCH_TRIGGER = 131,
    PXR_CONTROLLER_TOUCH_THUMB = 132,
    PXR_CONTROLLER_TOUCH_LASTONE = 255
} PxrControllerKeyMap;

typedef enum
{
    PXR_CONTROLLER_HAVE_TOUCH = 0x00000001,
    PXR_CONTROLLER_HAVE_GRIP = 0x00000002,
    PXR_CONTROLLER_HAVE_ROCKER = 0x00000004,
    PXR_CONTROLLER_HAVE_TOUCHPAD = 0x00000008,
    PXR_CONTROLLER_HAVE_ALL = 0xFFFFFFFF

} PxrControllerAbilities;


typedef enum {
    PXR_NO_DEVICE = 0,
    PXR_HB_Controller = 1,
    PXR_CV_Controller = 2,
    PXR_HB2_Controller = 3,
    PXR_CV2_Controller = 4,
    PXR_CV3_Optics_Controller = 5,
    PXR_CV3_Phoenix_Controller = 6,
    PXR_G3_Controller = 7,
    PXR_CV3_Hawk_Controller_Type = 8
} PxrControllerType;


typedef enum {
    PXR_NEO3_DEVICE = 1,
    PXR_PHOENIX_DEVICE = 2,
    PXR_MERLINE_DEVICE = 3,
} PxrHMDType;

typedef struct PxrControllerTracking_ {
    PxrSensorState localControllerPose;
    PxrSensorState globalControllerPose;
} PxrControllerTracking;


typedef struct PxrControllerInputState_ {
    PxrVector2f Joystick;   // 0-255
    int homeValue;          // 0/1
    int backValue;          // 0/1
    int touchpadValue;      // 0/1
    int volumeUp;           // 0/1
    int volumeDown;         // 0/1
    float triggerValue;       // 0-255 --> 0-1
    int batteryValue;       // 0-5
    int AXValue;            // 0/1
    int BYValue;            // 0/1
    int sideValue;          // 0/1
    float gripValue;          // 0-255  --> 0-1
    int triggerclickValue;    // 0/1
    int reserved_key_1;
    int reserved_key_2;
    int reserved_key_3;
    int reserved_key_4;

    int AXTouchValue;       // 0/1
    int BYTouchValue;       // 0/1
    int rockerTouchValue;   // 0/1
    int triggerTouchValue;  // 0/1
    int thumbrestTouchValue;// 0/1
    int reserved_touch_0;
    int reserved_touch_1;
    int reserved_touch_2;
    int reserved_touch_3;
    int reserved_touch_4;

} PxrControllerInputState;

typedef struct PxrControllerInputStateDowntimeStamp_ {

    long home;          // 0/1
    long back;          // 0/1
    long touchpad;      // 0/1
    long volumeUp;           // 0/1
    long volumeDown;         // 0/1
    long AX;            // 0/1
    long BY;            // 0/1
    long side;          // 0/1
    long grip;          // 0-255
    long reserved_key_0;
    long reserved_key_1;
    long reserved_key_2;
    long reserved_key_3;
    long reserved_key_4;

    long AXTouch;       // 0/1
    long BYTouch;       // 0/1
    long rockerTouch;   // 0/1
    long triggerTouch;  // 0/1
    long thumbrestTouch;// 0/1
    long reserved_touch_0;
    long reserved_touch_1;
    long reserved_touch_2;
    long reserved_touch_3;
    long reserved_touch_4;

} PxrControllerInputStateDowntimeStamp;


typedef struct  {
    PxrVector2f trackpad_value;
    int trackpad_click;//0-1
    int reserved_key_1;
    int reserved_key_2;
    int reserved_key_3;
    int reserved_key_4;
} PxrControllerKeyState;
typedef struct PxrInputEvent_ {
    union
    {
        int int_value;
        float  float_value;
    };
//    int int_value;
    bool up;
    bool down;
    bool shortpress;
    bool longpress;
} PxrInputEvent;

typedef struct PxrControllerInputEvent_ {
    PxrInputEvent home;          // 0/1
    PxrInputEvent back;          // 0/1
    PxrInputEvent touchpad;      // 0/1
    PxrInputEvent volumeUp;      // 0/1
    PxrInputEvent volumeDown;    // 0/1
    PxrInputEvent AX;            // 0/1
    PxrInputEvent BY;            // 0/1
    PxrInputEvent side;          // 0/1
    PxrInputEvent reserved_0_Key;// 0/1
    PxrInputEvent reserved_1_Key;// 0/1
    PxrInputEvent reserved_2_Key;// 0/1
    PxrInputEvent reserved_3_Key;// 0/1
    PxrInputEvent reserved_4_Key;// 0/1

    PxrInputEvent AXTouch;       // 0/1
    PxrInputEvent BYTouch;       // 0/1
    PxrInputEvent rockerTouch;   // 0/1
    PxrInputEvent triggerTouch;  // 0/1
    PxrInputEvent thumbrestTouch;// 0/1
    PxrInputEvent reserved_0_Touch;// 0/1
    PxrInputEvent reserved_1_Touch;// 0/1
    PxrInputEvent reserved_2_Touch;// 0/1
    PxrInputEvent reserved_3_Touch;// 0/1
    PxrInputEvent reserved_4_Touch;// 0/1

} PxrControllerInputEvent;


typedef struct PxrControllerCapability_ {
    PxrControllerType             type;
    PxrControllerDof              Dof;
    PxrControllerBond             inputBond;
    uint64_t                 Abilities;
} PxrControllerCapability;

typedef struct PxrControllerInfo_ {
    PxrControllerType             type;
    char* mac;
    char* sn;
    char* version;
} PxrControllerInfo;

//Handtracking data
typedef enum {
    pxrHeadActive               = 0,
    pxrControllerActive         = 1,
    pxrHandTrackingActive       = 2,
}PxrActiveInputDeviceType;
typedef enum{
    PxrNone = -1,
    PxrHandLeft = 0,
    PxrHandRight = 1,
}PxrHandType;
typedef enum{
    PxrSkeletonTypeNone = -1,
    PxrSkeletonTypeHandLeft = 0,
    PxrSkeletonTypeHandRight = 1,
} PxrSkeletonType;
typedef enum {
    PxrMeshTypeNone = -1,
    PxrMeshTypeHandLeft = 0,
    PxrMeshTypeHandRight = 1,
} PxrMeshType;
typedef enum PxrHandTrackingStatus_{
    PxrHandTracked = (1 << 0),
    PxrInputStateValid = (1 << 1),
    PxrSystemGestureInProgress = (1 << 6),
    PxrDominantHand = (1 << 7),
    PxrMenuPressed = (1 << 8)
}PxrHandTrackingStatus;
typedef struct PxrVector4s_ {
    int16_t x, y, z, w;
}PxrVector4s;
typedef enum PxrHandBoneIndex_{
    PxrHandBone_Invalid = -1,
    PxrHandBone_WristRoot = 0, // root frame of the hand, where the wrist is located
    PxrHandBone_ForearmStub = 1, // frame for user's forearm
    PxrHandBone_Thumb0 = 2, // thumb trapezium bone
    PxrHandBone_Thumb1 = 3, // thumb metacarpal bone
    PxrHandBone_Thumb2 = 4, // thumb proximal phalange bone
    PxrHandBone_Thumb3 = 5, // thumb distal phalange bone
    PxrHandBone_Index1 = 6, // index proximal phalange bone
    PxrHandBone_Index2 = 7, // index intermediate phalange bone
    PxrHandBone_Index3 = 8, // index distal phalange bone
    PxrHandBone_Middle1 = 9, // middle proximal phalange bone
    PxrHandBone_Middle2 = 10, // middle intermediate phalange bone
    PxrHandBone_Middle3 = 11, // middle distal phalange bone
    PxrHandBone_Ring1 = 12, // ring proximal phalange bone
    PxrHandBone_Ring2 = 13, // ring intermediate phalange bone
    PxrHandBone_Ring3 = 14, // ring distal phalange bone
    PxrHandBone_Pinky0 = 15, // pinky metacarpal bone
    PxrHandBone_Pinky1 = 16, // pinky proximal phalange bone
    PxrHandBone_Pinky2 = 17, // pinky intermediate phalange bone
    PxrHandBone_Pinky3 = 18, // pinky distal phalange bone
    PxrHandBone_MaxSkinnable = 19,
    // Bone tips are position only. They are not used for skinning but useful for hit-testing.
    // NOTE: HandBone_ThumbTip == HandBone_MaxSkinnable since the extended tips need to be
    // contiguous

    PxrHandBone_ThumbTip = PxrHandBone_MaxSkinnable + 0, // tip of the thumb
    PxrHandBone_IndexTip = PxrHandBone_MaxSkinnable + 1, // tip of the index finger
    PxrHandBone_MiddleTip = PxrHandBone_MaxSkinnable + 2, // tip of the middle finger
    PxrHandBone_RingTip = PxrHandBone_MaxSkinnable + 3, // tip of the ring finger
    PxrHandBone_PinkyTip = PxrHandBone_MaxSkinnable + 4, // tip of the pinky
    PxrHandBone_Max = PxrHandBone_MaxSkinnable + 5,
} PxrHandBoneIndex;
#define PxrHandBoneIndex_max 24

typedef enum {
	PxrHandPinch_Thumb = 1 << 0,
	PxrHandPinch_Index = 1 << 1,
	PxrHandPinch_Middle = 1 << 2,
	PxrHandPinch_Ring = 1 << 3,
	PxrHandPinch_Pinky = 1 << 4,
} PxrHandFingerPinch;
#define PxrHandFingerPinch_max 5

typedef enum {
	PxrTrackingConfidence_LOW,
	PxrTrackingConfidence_HIGH,
} PxrTrackingConfidence;
#define PxrHandFinger_Max 5

//new handtracking
#define PxrHandJointCount 26
typedef struct
{
	uint64_t    	locationFlags;
	PxrPosef        pose;
	float           radius;
} PxrHandJointsLocation;
typedef struct
{
	uint32_t                   isActive;
	uint32_t                   jointCount;
	float    		  	       HandScale;
	PxrHandJointsLocation      jointLocations[PxrHandJointCount];
}PxrHandJointsLocations;

typedef struct handaimstate_ {
	uint64_t  			Status;
	PxrPosef      	    aimPose;
	float             	pinchStrengthIndex;
	float            	pinchStrengthMiddle;
	float             	pinchStrengthRing;
	float             	pinchStrengthLittle;
	float             	ClickStrength;
}PxrHandAimState;

typedef struct Pxrhandstate_ {
	int16_t  Status;
	PxrPosef   RootPose;
	PxrPosef   BonePose[PxrHandBoneIndex_max];
	int16_t Pinches;
	float    PinchStrength[PxrHandFingerPinch_max];
	float ClickStrength;
	PxrPosef   PointerPose;
	float    HandScale;
	PxrTrackingConfidence  HandConfidence;
	PxrTrackingConfidence  FingerConfidence[PxrHandFinger_Max];
	double   RequestedTimeStamp;
	double   SampleTimeStamp;
}PxrHandState;

typedef struct PxrBoneCapsule_ {
	PxrHandBoneIndex BoneIndex;
	PxrVector3f StartPoint;
	PxrVector3f EndPoint;
	float Radius;
} PxrBoneCapsule;

typedef struct Bone_ {
	PxrPosef  Bones;
	PxrHandBoneIndex BoneIndices;
	PxrHandBoneIndex ParentBoneIndices;
}PxrBone;
#define PxrBoneCapsule_max 19
typedef struct PxrSkeleton_ {
	PxrSkeletonType Type;
	int NumBones;
	int NumBoneCapsules;
	PxrBone     Bones[PxrHandBoneIndex_max];
	PxrBoneCapsule   Capsules[PxrBoneCapsule_max];
}PxrSkeleton;

typedef int16_t PxrVertexIndex;
#define PxrHand_MaxVertices 3000
#define PxrHand_MaxIndices  PxrHand_MaxVertices*6
typedef struct {
	int  NumVertices;
	int  NumIndices;
	PxrVector3f   VertexPositions[PxrHand_MaxVertices];
	PxrVertexIndex   Indices[PxrHand_MaxIndices];
	PxrVector3f    VertexNormals[PxrHand_MaxVertices];
	PxrVector2f    VertexUV0[PxrHand_MaxVertices];
	PxrVector4s    BlendIndices[PxrHand_MaxVertices];
	PxrVector4f    BlendWeights[PxrHand_MaxVertices];
}PxrHandMesh;

//Handtracking data

/*************************************** Motion Tracking ***************************************/
typedef enum {
	PXR_MT_SUCCESS               =  0,
	PXR_MT_FAILURE               = -1,
	PXR_MT_MODE_NONE             = -2,
	PXR_MT_DEVICE_NOT_SUPPORT    = -3,
	PXR_MT_SERVICE_NEED_START    = -4,
	PXR_MT_ET_PERMISSION_DENIED  = -5,
	PXR_MT_FT_PERMISSION_DENIED  = -6,
	PXR_MT_MIC_PERMISSION_DENIED = -7,
	PXR_MT_SYSTEM_DENIED         = -8,
	PXR_MT_UNKNOW_ERROR          = -9
} PxrTrackingStateCode;

/*************************************** Face Tracking ***************************************/
#define PXR_FACE_TRACKING_API_VERSION 1
#ifdef PXR_FACE_TRACKING_API_VERSION 

typedef enum {
	PXR_FTM_NONE          =-1,
	PXR_FTM_FACE          = 0,
	PXR_FTM_LIPS          = 1,
	PXR_FTM_FACE_LIPS_VIS = 2,
	PXR_FTM_FACE_LIPS_BS  = 3,
	PXR_FTM_COUNT         = 4
} PxrFaceTrackingMode;

typedef int64_t PxrFaceTrackingDataGetFlags;
static const PxrFaceTrackingDataGetFlags PXR_FACE_DEFAULT = 0;

typedef struct PxrFaceTrackingStartInfo_ {
	int                         apiVersion;
	PxrFaceTrackingMode         mode;
} PxrFaceTrackingStartInfo;

typedef struct PxrFaceTrackingStopInfo_ {
	int                         apiVersion;
	bool                        pause;
} PxrFaceTrackingStopInfo;

typedef struct PxrFaceTrackignState_ {
	int                         apiVersion;
	PxrFaceTrackingMode         currentTrackingMode;
	PxrTrackingStateCode        code;
} PxrFaceTrackingState;

typedef struct PxrFaceTrackingDataGetInfo_ {
	int                         apiVersion;
	int64_t                     displayTime;
    PxrFaceTrackingDataGetFlags flags;
} PxrFaceTrackingDataGetInfo;

typedef struct PxrFaceTrackingData_ {
	int                         apiVersion;
	float*                      blendShapeWeight;
	int64_t                     timestamp;
	float                       laughingProb;
	bool                        eyeValid;
	bool                        faceValid;
} PxrFaceTrackingData;

#endif // PXR_FACE_TRACKING_API_VERSION

/*************************************** Eye Tracking ***************************************/
#define PXR_EYE_TRACKING_API_VERSION 1
#ifdef PXR_EYE_TRACKING_API_VERSION

typedef enum {
	PXR_ETM_NONE  =-1,
	PXR_ETM_BOTH  = 0,
	PXR_ETM_COUNT = 1
} PxrEyeTrackingMode;

typedef enum {
	leftEye =  0,
	rightEye = 1,
	combined = 2,
	eyeCount = 3
} PxrPerEyeUsage;

typedef int64_t PxrEyeTrackingDataGetFlags;
static const PxrEyeTrackingDataGetFlags PXR_EYE_DEFAULT     = 0;
static const PxrEyeTrackingDataGetFlags PXR_EYE_POSITION    = 1 << 0;
static const PxrEyeTrackingDataGetFlags PXR_EYE_ORIENTATION = 1 << 1;

typedef struct PxrEyeTrackingStartInfo_ {
	int                        apiVersion;
	bool                       needCalibration;
	PxrEyeTrackingMode         mode;
} PxrEyeTrackingStartInfo;

typedef struct PxrEyeTrackingStopInfo_ {
	int                        apiVersion;
} PxrEyeTrackingStopInfo;

typedef struct PxrEyeTrackingState_ {
	int                        apiVersion;
	PxrEyeTrackingMode         currentTrackingMode;
	PxrTrackingStateCode       code;
} PxrEyeTrackingState;

typedef struct PxrEyeTrackingDataGetInfo_ {
	int                        apiVersion;
	int64_t                    displayTime;
	PxrEyeTrackingDataGetFlags flags;
} PxrEyeTrackingDataGetInfo;

typedef struct PxrPerEyeData_ {
	int                        apiVersion;
	PxrPosef                   pose;
  //float                      confidence;
	bool                       isPoseValid;
	float                      openness;
	bool                       isOpennessValid;
} PxrPerEyeData;

typedef struct PxrEyeTrackingData1_ {
	int                        apiVersion;
	PxrPerEyeData              eyeDatas[eyeCount];
  //double                     timestamp;
} PxrEyeTrackingData1;

#endif // PXR_EYE_TRACKING_API_VERSION

/*************************************** Body Tracking ***************************************/
#define PXR_BODY_TRACKING_API_VERSION 1

#ifdef PXR_BODY_TRACKING_API_VERSION
//Bodytracking data
typedef enum BodyTrackerRole
{
    PxrPelvis         = 0,
    Pxr_LEFT_HIP       = 1,
    Pxr_RIGHT_HIP      = 2,
    Pxr_SPINE1         = 3,
    Pxr_LEFT_KNEE      = 4,
    Pxr_RIGHT_KNEE     = 5,
    Pxr_SPINE2         = 6,
    Pxr_LEFT_ANKLE     = 7,
    Pxr_RIGHT_ANKLE    = 8,
    Pxr_SPINE3         = 9,
    Pxr_LEFT_FOOT      = 10,
    Pxr_RIGHT_FOOT     = 11,
    Pxr_NECK           = 12,
    Pxr_LEFT_COLLAR    = 13,
    Pxr_RIGHT_COLLAR   = 14,
    Pxr_HEAD           = 15,
    Pxr_LEFT_SHOULDER  = 16,
    Pxr_RIGHT_SHOULDER = 17,
    Pxr_LEFT_ELBOW     = 18,
    Pxr_RIGHT_ELBOW    = 19,
    Pxr_LEFT_WRIST     = 20,
    Pxr_RIGHT_WRIST    = 21,
    Pxr_LEFT_HAND      = 22,
    Pxr_RIGHT_HAND     = 23,
    Pxr_NONE_ROLE      = 24,
    Pxr_MIN_ROLE       = 0,
    Pxr_MAX_ROLE       = 23,
    Pxr_ROLE_NUM       = 24,
} PxrBodyTrackerRole;

// imu data
typedef struct PxrBodyTrackingImu
{
    int64_t TimeStamp;                // time stamp of imu
    double    temperature;              // temperature of imu
    double    GyroData[3];              // gyroscope data, x,y,z
    double    AccData[3];               // Accelerometer data, x,y,z
    double    MagData[3];               // magnetometer data, x,y,z
} PxrBodyTrackingImu;

typedef struct PxrBodyTrackingPose
{
    int64_t TimeStamp;                // time stamp of imu
    double    PosX;                     // position of x
    double    PosY;                     // position of y
    double    PosZ;                     // position of z
    double    RotQx;                    // x components of Quaternion
    double    RotQy;                    // y components of Quaternion
    double    RotQz;                    // z components of Quaternion
    double    RotQw;                    // w components of Quaternion
} PxrBodyTrackingPose;

typedef enum {
    PXR_BTM_DISABLE             =0,
    PXR_BTM_WITH_SWIFT          =1,
    PXR_BTM_WITHOUT_SWIFT       =2,
    PXR_BTM_MAX_ENUM            = 0x7FFFFFFF
}PxrBodyTrackingMode;
typedef enum {
    Pxr_HUMAN_HEIGHT = 0,
    Pxr_SWIFT_MODE = 1,
    Pxr_BONE_PARAM = 2
} PxrBodyTrackingAlgParamType;
typedef struct PxrBodyTrackingBoneLength
{
    float HeadLen;
    float NeckLen;
    float TorsoLen;
    float HipLen;
    float UpperLegLen;
    float LowerLegLen;
    float FootLen;
    float ShoulderLen;
    float UpperArmLen;
    float LowerArmLen;
    float HandLen;
} PxrBodyTrackingBoneLength;
typedef struct PxrBodyTrackingAlgParam
{
    int32_t  BodyJointSet;
    PxrBodyTrackingBoneLength BoneLength;
}PxrBodyTrackingAlgParam;
typedef struct PxrBodyTrackingStartInfo_ {
    int                         apiVersion;
    PxrBodyTrackingMode         mode;
    int32_t  BodyJointSet;
    PxrBodyTrackingBoneLength BoneLength;
}PxrBodyTrackingStartInfo;

typedef struct PxrBodyTrackingStopInfo_ {
    int                         apiVersion;
}PxrBodyTrackingStopInfo;

typedef enum {
    Pxr_BODYTRACKING_INVALID = 0,
    Pxr_BODYTRACKING_VALID = 1,
    Pxr_BODYTRACKING_LIMITED = 2
} PxrBodyTrackingStatusCode;

typedef enum {
    Pxr_BT_ERROR_INNER_EXCEPTION = 0,
    Pxr_BT_ERROR_TRACKER_NOT_CALIBRATED = 1,
    Pxr_BT_ERROR_TRACKER_NUM_NOT_ENOUGH = 2,
    Pxr_BT_ERROR_TRACKER_STATE_NOT_SATISFIED = 3,
    Pxr_BT_ERROR_TRACKER_PERSISTENT_INVISIBILITY = 4,
    Pxr_BT_ERROR_TRACKER_DATA_ERROR = 5,
    Pxr_BT_ERROR_USER_CHANGE = 6,
    Pxr_BT_ERROR_TRACKING_POSE_ERROR = 7,
} PxrBodyTrackingErrorCode;


typedef struct PxrBodyTrackingState_ {
    int                         apiVersion;
    PxrBodyTrackingMode         currentTrackingMode;
    PxrTrackingStateCode        code;
    PxrBodyTrackingStatusCode   StateCode;
    PxrBodyTrackingErrorCode    ErrorCode;
    uint8_t                     connectedBandCount;//swift计数?
    uint8_t                     fitnessBand[12];//绑定id  12个数字
} PxrBodyTrackingState;

typedef enum {
    PXR_BODY_NONE               = 0,
    PXR_BODY_POSE               = 1 << 0,
    PXR_BODY_ACTION             = 1 << 1,
    PXR_BODY_VELO_ACC           = 1 << 2,
    PXR_BODY_MAX_ENUM           = 0x7FFFFFFF
} PxrBodyTrackingGetDataFlags;

typedef struct PxrBodyTrackingGetDataInfo_ {
    int                         apiVersion;
    int64_t                     displayTime;
    PxrBodyTrackingGetDataFlags flags; //use in future
} PxrBodyTrackingGetDataInfo;
// action set
typedef enum BodyActionList_
{
	PxrNoneAction  = 0x00000000,
	PxrTouchGround = 0x00000001,
	PxrKeepStatic  = 0x00000002,
	PxrTouchGroundToe =0x00000004,
	PxrFootDownAction =0x00000008
  } PxrBodyActionList;

typedef struct PxrBodyTrackingRoleData_ {
	int                         apiVersion;
	PxrBodyTrackerRole          role;
	PxrBodyActionList           bodyAction;
	PxrBodyTrackingPose         localPose;
	PxrBodyTrackingPose         globalPose;
	double                      velo[3];
	double                      acce[3];
	double                      wvelo[3];
	double                      wacce[3];
} PxrBodyTrackingRoleData;

typedef struct PxrBodyTrackingData_ {
	int                           apiVersion;
	PxrBodyTrackingRoleData       roleData[24];
} PxrBodyTrackingData;

typedef struct PxrBodyTrackingTransform
{
  PxrBodyTrackerRole bone;                // bone name. if bone == NONE_ROLE, this bone is not calculated
  PxrBodyTrackingPose localpose;
  PxrBodyTrackingPose globalpose;
  double velo[3];                     // velocity of x,y,z
  double acce[3];                     // acceleration of x,y,z
  double wvelo[3];                    // angular velocity of x,y,z
  double wacce[3];                    // angular acceleration of x,y,z
  uint32_t   bodyAction;              // multiple actions can be supported at the same time by means of OR BodyActionList
} PxrBodyTrackingTransform;


typedef struct PxrBodyTrackingResult
{
    PxrBodyTrackingTransform trackingdata[Pxr_ROLE_NUM];
} PxrBodyTrackingResult;

typedef struct PxrFitnessBandConnectState
{
	uint8_t num;
	uint8_t trackerID[12];
} PxrFitnessBandConnectState;

typedef struct
{
	int trackerSum;
	char trackersSN[6][24];
}PxrMotionConnectState;

typedef enum
{
	PxrUnknown = 0,
	PxrSwift_1 = 1,
	PxrSwift_2
}PxrMotionTrackerType;

typedef enum
{
	PxrBodyTracking = 0,
	PxrMotionTracking
} PxrMotionTrackerMode;

typedef struct
{
	PxrPosef      pose;
	float angularVelocity[3];
	float linearVelocity[3];
	float angularAcceleration[3];
	float linearAcceleration[3];
}PxrMotionTrackerPoseLocation;

typedef struct
{
	char trackerSN[24];
	PxrMotionTrackerPoseLocation      localPose;  
	PxrMotionTrackerPoseLocation        globalPose;

}PxrMotionTrackerLocations;


typedef struct
{
	char trackerSN[24];
	uint8_t chargerStatus;
	uint8_t batteryVolume;
}PxrExtDevTrackerInfo;

typedef struct
{
	int extNumber;
	PxrExtDevTrackerInfo info[6];
}PxrExtDevTrackerConnectState;

typedef struct
{
	char trackerSN[24];
	int32_t level;
	int32_t frequency;
	int32_t duration;
}PxrExtDevTrackerMotorVibrate;

typedef struct
{
	char trackerSN[24];
	uint8_t passData[15];
}PxrExtDevTrackerPassData;

typedef struct {
	int home;
	int app;
	int a_x;
	int b_y;
	int grip;
	int rocker;
	int trigger;
} PxrExtDevTrackerKey;

typedef struct {
	int a_x;
	int b_y;
	int rocker;
	int trigger;
	int thumbrest;
} PxrExtDevTrackerTouch;
typedef struct
{
	int extDevID; //
	PxrExtDevTrackerKey key;
	PxrExtDevTrackerTouch touch;

	uint8_t trigger;
	uint8_t grip;
	uint8_t rocker_x;
	uint8_t rocker_y;

}PxrExtDevTrackerKeyData;

#endif

/*************************************** MR ***************************************/
// add mr persistence structures definition
// Spatial entity uuid 128 bit
typedef struct PxrSpatialInstanceUuid
{
	uint64_t value[PXR_UUID_SIZE];
} PxrSpatialInstanceUuid;

// Create info struct used when creating a spatial anchor
typedef struct PxrSpatialAnchorCreateInfoV1
{
	PxrReferenceType referenceType; // Global or Local
	PxrPosef pose;                  // Position and Rotation
	double timeMs;                  // timestamp in milliseconds
} PxrSpatialAnchorCreateInfoV1;

// Save info struct used when saving a spatial anchor
typedef struct PxrSpatialAnchorSaveInfo
{
	uint64_t anchorHandle;
	PxrSpatialPersistenceLocation location;
	PxrSpatialPersistenceMode persistenceMode;
} PxrSpatialAnchorSaveInfo;

// Delete info struct used when deleting a spatial entity
typedef struct PxrSpatialAnchorDeleteInfo
{
	uint64_t anchorHandle;
	PxrSpatialPersistenceLocation persistenceLocation;
} PxrSpatialAnchorDeleteInfo;

// Used to load all spatial instances that match the uuids provided
// in the filter info. If numIds is set to 0, all Ids will be loaded.
typedef struct PxrSpatialInstanceIdFilter
{
	uint32_t numIds;
	PxrSpatialInstanceUuid uuids[1024];
} PxrSpatialInstanceIdFilter;

// Load info strut used when loading spatial instances
typedef struct PxrSpatialInstanceLoadByIdInfo
{
	uint32_t maxNum;                                   // Max num of handles can be returned by this query
	double timeoutMs;                                  // time in milliseconds
	PxrSpatialPersistenceLocation persistenceLocation; // Where to load
	const PxrSpatialInstanceIdFilter idFilter;         // What to load
} PxrSpatialInstanceLoadByIdInfo;

// Path name used to export or import the spatial entity persistence files.
#define PXR_MAX_SPATIAL_PERSISTENCE_PATH_NAME_SIZE 256
typedef struct PxrSpatialPersistencePathName
{
	char name[PXR_MAX_SPATIAL_PERSISTENCE_PATH_NAME_SIZE];
} PxrSpatialPersistencePathName;

// Export info used to export the spatial instance persistence files.
typedef struct PxrExportSpatialInstanceInfo
{
	PxrSpatialPersistencePathName *pathName;
	PxrSpatialPersistenceLocation fromLocation;
	uint64_t timeout;
} PxrExportSpatialInstanceInfo;

// Import info used to export the spatial Instance persistence files.
typedef struct PxrImportSpatialInstanceInfo
{
	PxrSpatialPersistencePathName *pathName;
	PxrSpatialPersistenceLocation toLocation;
	uint64_t timeout;
} PxrImportSpatialInstanceInfo;

#define PXR_SPATIAL_MODEL_DATA_MAX_SIZE 1024
#define PXR_SPATIAL_ANCHOR_LOAD_MAX_RESULTS_PER_EVENT 128
#define PXR_SPATIAL_MODEL_LOAD_MAX_RESULTS_PER_EVENT 64

typedef struct PxrSpatialModelData
{
	char data[PXR_SPATIAL_MODEL_DATA_MAX_SIZE];
} PxrSpatialModelData;

// Create info struct used when creating an object model
typedef struct PxrSpatialModelCreateInfo
{
	double timeMs;                  // timestamp in milliseconds
	PxrSpatialModelData data; // Binary data of the object model.
} PxrSpatialModelCreateInfo;

// Save info struct used when saving an object model
typedef struct PxrSpatialModelSaveInfo
{
	uint64_t modelHandle;
	PxrSpatialPersistenceLocation location;
	PxrSpatialPersistenceMode persistenceMode;
} PxrSptialModelSaveInfo;

// Delete info struct used when deleting an object model
typedef struct PxrSpatialModelDeleteInfo
{
	uint64_t modelHandle;
	PxrSpatialPersistenceLocation persistenceLocation;
} PxrSpatialModelDeleteInfo;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_ANCHOR_SAVE_RESULT = (PxrStructureType)20;
typedef struct PxrEventSpatialAnchorSaveResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	PxrSpatialInstanceUuid uuid;
	uint64_t handle;
	PxrSpatialPersistenceLocation location;
} PxrEventSpatialAnchorSaveResult;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_ANCHOR_DELETE_RESULT = (PxrStructureType)21;
typedef struct PxrEventSpatialAnchorDeleteResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	PxrSpatialInstanceUuid uuid;
	PxrSpatialPersistenceLocation location;
} PxrEventSpatialAnchorDeleteResult;

// Load result to be returned in the results array of PxrEventSpatialInstanceResults
typedef struct PxrSpatialAnchorLoadResult
{
	uint64_t anchorHandle;
	PxrSpatialInstanceUuid uuid;
} PxrSpatialAnchorLoadResult;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_ANCHOR_LOAD_RESULTS = (PxrStructureType)22;
//  Returned when some number of query results are available.
typedef struct PxrEventSpatialAnchorLoadResults
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	bool hasNext;  //indicate if there's a next load event or load complete
	uint64_t asyncRequestId;
	uint32_t numResults;
	PxrSpatialAnchorLoadResult loadResults[PXR_SPATIAL_ANCHOR_LOAD_MAX_RESULTS_PER_EVENT];
} PxrEventSpatialAnchorLoadResults;

typedef struct PxrSpatialAnchorLoadResults {
	uint32_t resultCapacityInput;
	uint32_t resultCountOutput;
	PxrSpatialAnchorLoadResult *results;
} PxrSpatialAnchorLoadResults;

// Returned when there's any anchor load result is available.
typedef struct PxrEventSpatialAnchorLoadResultsAvailable
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	uint64_t asyncRequestId;
} PxrEventSpatialAnchorLoadResultsAvailable;

// Returned when all anchor load results are available.
typedef struct PxrEventSpatialAnchorLoadResultsComplete
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	uint64_t asyncRequestId;
	PxrSpatialPersistenceResult result;
} PxrEventSpatialAnchorLoadResultsComplete;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_MODEL_SAVE_RESULT = (PxrStructureType)24;
typedef struct PxrEventDataSpatialModelSaveResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	PxrSpatialInstanceUuid uuid;
	uint64_t handle;
	PxrSpatialPersistenceLocation location;
} PxrEventDataSpatialModelSaveResult;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_MODEL_DELETE_RESULT = (PxrStructureType)25;
typedef struct PxrEventDataSpatialModelDeleteResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	PxrSpatialInstanceUuid uuid;
	PxrSpatialPersistenceLocation location;
} PxrEventDataSpatialModelDeleteResult;

// Load result to be returned in the results array of PxrEventSpatialInstanceResults
typedef struct PxrSpatialModelLoadResult
{
	uint64_t modelHandle;
	PxrSpatialInstanceUuid uuid;
} PxrSpatialModelLoadResult;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_MODEL_LOAD_RESULTS = (PxrStructureType)26;
typedef struct PxrEventDataSpatialModelLoadResults
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	bool hasNext; //indicate if there's a next load event or load complete
	uint64_t asyncRequestId;
	uint32_t numResult;
	PxrSpatialModelLoadResult loadResults[PXR_SPATIAL_MODEL_LOAD_MAX_RESULTS_PER_EVENT];
} PxrEventDataSpatialModelLoadResults;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_INSTANCE_PERSISTENCE_EXPORT_COMPLETE = (PxrStructureType)28;
typedef struct PxrEventDataSpatialInstancePersistenceExportComplete
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t request;
} PxrEventDataSpatialInstancePersistenceExportComplete;

// static const PxrStructureType PXR_TYPE_EVENT_SPATIAL_INSTANCE_PERSISTENCE_IMPORT_COMPLETE = (PxrStructureType)29;
typedef struct PxrEventDataSpatialInstancePersistenceImportComplete
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t request;
} PxrEventDataSpatialInstancePersistenceImportComplete;

// PxrNewSpaceType
typedef enum PxrNewSpaceType
{
	PXR_NEW_SPACE_TYPE_NEW_CREATED = 0,
	PXR_NEW_SPACE_TYPE_RECOGNIZED = 1,
	PXR_NEW_SPACE_TYPE_RECOGNIZED_WITH_ROOM_SCENE = 2,
} PxrNewSpaceType;

// static const PxrStructureType PXR_TYPE_EVENT_NEW_SPACE_READY = (PxrStructureType)30;
typedef struct PxrEventNewSpaceReady {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	PxrNewSpaceType              state;
} PxrEventNewSpaceReady;

// static const PxrStructureType PXR_TYPE_EVENT_SPACE_OPTIMIZED_STATUS = (PxrStructureType)31;
typedef struct PxrEventSpaceOptimizedStatus {
	PxrStructureType             type;
	PxrEventLevel                eventLevel;
	uint32_t		             state;
} PxrEventSpaceOptimizedStatus;

typedef struct PxrEventTrackingStateNotification{
    PxrStructureType            type;
    PxrEventLevel               eventLevel;
    uint32_t                    status;
    uint32_t                    msg;
}PxrEventTrackingStateNotification;

// Save info struct used when saving
typedef struct PxrRoomSceneDataSaveInfo
{
	uint64_t roomSceneDataHandle;
	PxrSpatialPersistenceLocation location;
} PxrRoomSceneDataSaveInfo;

// Delete info struct used when deleting
typedef struct PxrRoomSceneDataDeleteInfo
{
	uint64_t roomSceneDataHandle;
	PxrSpatialPersistenceLocation location;
}PxrRoomSceneDataDeleteInfo;

typedef struct PxrEventRoomSceneDataSaveResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	uint64_t handle;
	PxrSpatialPersistenceLocation location;
} PxrEventRoomSceneDataSaveResult;


typedef struct PxrEventRoomSceneDataDeleteResult
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	PxrSpatialPersistenceResult result;
	uint64_t asyncRequestId;
	uint64_t handle;
	PxrSpatialPersistenceLocation location;
} PxrEventRoomSceneDataDeleteResult;


// Load info strut used when loading
typedef struct PxrRoomSceneLoadInfo
{
	double timeoutMs;                           // time in milliseconds
	PxrSpatialPersistenceLocation location;     // Where to load
} PxrRoomSceneLoadInfo;


// Load result to be returned in the results array of PxrEventSpatialInstanceResults
#define ROOM_SCENE_DATA_MAX_SIZE 1024
typedef struct PxrRoomSceneLoadResult
{
	uint64_t anchorHandle;
	PxrSpatialInstanceUuid anchorUuid;
	uint64_t roomSceneDataHandle;
	int dataLen;
	uint8_t data[ROOM_SCENE_DATA_MAX_SIZE];
} PxrRoomSceneLoadResult;

// Returned when all Scene data load results are available.
typedef struct PxrEventRoomSceneLoadResultsComplete
{
	PxrStructureType type;
	PxrEventLevel eventLevel;
	uint64_t asyncRequestId;
	PxrSpatialPersistenceResult result;
} PxrEventRoomSceneLoadResultsComplete;

// Returned when call Pxr_GetRoomSceneLoadResults
// If set resultCapacityInput 0, will return the available
// result count in resultCountOutput, so that the caller can
// allocate proper buffer size and trigger the 2nd call to get
// the results.
typedef struct PxrRoomSceneLoadResults {
	uint32_t resultCapacityInput;
	uint32_t resultCountOutput;
	PxrRoomSceneLoadResult* results;
} PxrRoomSceneLoadResults;

typedef struct XrRoomSceneData{
    uint64_t anchorHandle;
    uint8_t uuid[16];
}XrRoomSceneData;

typedef struct XrRoomSceneDataSet{
    XrRoomSceneData floor;
    XrRoomSceneData ceiling;
    uint32_t wallCapacityInput;
    uint32_t* wallCountOutput;
    XrRoomSceneData* walls;
    uint32_t doorCapacityInput;
    uint32_t* doorCountOutput;
    XrRoomSceneData* doors;
    uint32_t windowCapacityInput;
    uint32_t* windowCountOutput;
    XrRoomSceneData*  windows;
    uint32_t otherCapacityInput;
    uint32_t* otherCountOutput;
    XrRoomSceneData* others;
} XrRoomSceneDataSet;

typedef struct PxrUUid
{
    uint64_t value[PXR_UUID_SIZE];
} PxrUuid;

//mr sense pack 1.2 mr map management
typedef enum PxrSpatialAnchorEntityType {
    PxrSceneAnchor = 0,
    PxrNormalAnchor,
} PxrSpatialAnchorEntityType;

typedef struct PxrEventDataSpatialAnchorDetected{
    PxrStructureType type;//PXR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_DETECTED
    PxrEventLevel eventLevel;
    PxrSpatialAnchorEntityType anchorType;
} PxrEventDataSpatialAnchorDetected;

typedef struct PxrAnchorEntityUuidList{
    uint32_t            count;
    PxrUuid*            uuidList;
} PxrAnchorEntityUuidList ;

typedef struct PxrAnchorEntityUnpersistUuidInfo{
    PxrAnchorEntityUuidList    anchorList; //uuids
    PxrPersistLocation     location; //Persist location(local or remote)
} PxrAnchorEntityUnpersistUuidInfo ;

typedef enum PxrSpatialMapSizeLimitedReason {
    MapSizeLimitedUnknown = 0,
    MapQuantitySizeLimited,
    SingleMapSizeLimited,
    TotalMapSizeLimited,
} XrSpatialMapSizeLimitedReason;

typedef struct PxrEventDataSpatialMapSizeLimited{
    PxrStructureType type;//PXR_TYPE_EVENT_DATA_SPATIAL_MAP_SIZE_LIMITED
    PxrEventLevel eventLevel;
    PxrSpatialMapSizeLimitedReason reason;
}PxrEventDataSpatialMapSizeLimited;

//mr v2.0
typedef struct PxrSpatialTrackingStateInfo{
    PxrSpatialTrackingState state;
    PxrSpatialTrackingStateMessage message;
} PxrSpatialTrackingStateInfo;

typedef struct PxrEventDataSpatialTrackingStateUpdate{
    PxrStructureType type;//PXR_TYPE_EVENT_DATA_SPATIAL_TRACKING_STATE_UPDATE
    PxrEventLevel eventLevel;
    PxrSpatialTrackingStateInfo stateInfo;
} PxrEventDataSpatialTrackingStateUpdate;

typedef struct PxrAnchorEntityCreateInfo {
    PxrTrackingOrigin pxrTrackingOrigin;
    PxrPosef pose; //Pose in reference space
    double timeMs; //Timestamp of the pose *1000*1000
} PxrAnchorEntityCreateInfo ;
typedef struct PxrAnchorEntityDestroyInfo {
    uint64_t anchor;
} PxrAnchorEntityDestroyInfo ;

typedef struct PxrAnchorComponentInfoBaseHeader {
    PxrStructureType    type;
    uint8_t             varying[128];
} PxrAnchorComponentInfoBaseHeader;
typedef struct PxrAnchorComponentAddInfo {
    PxrStructureType                    type;//PXR_TYPE_SPATIAL_ENTITY_COMPONENT_SET_INFO
    uint64_t                   anchor;
    PxrAnchorComponentType            compType;
    PxrAnchorComponentInfoBaseHeader* compData; // if compData == nullptr , remove  Component with compType.
} PxrAnchorComponentAddInfo ;
typedef struct PxrAnchorComponentSceneLabelInfo{
    PxrStructureType    type;//PXR_TYPE_ANCHOR_COMPONENT_SCENE_LABEL_INFO
    PxrSceneLabel     label;
} PxrAnchorSceneLabelComponentInfo ;
typedef struct PxrExtent2Df {
    float    width;
    float    height;
} PxrExtent2Df;
typedef struct PxrExtent2Di {
    int width;
    int height;
} PxrExtent2Di;



typedef struct PxrAnchorComponentPlaneInfo{
    PxrStructureType    type;//PXR_TYPE_ANCHOR_COMPONENT_PLANE_INFO
    PxrVector3f         center;
    PxrExtent2Df        extent;
    uint32_t            polygonSize;
    PxrVector3f*        polygonVertices;
} PxrAnchorPlaneComponentInfo ;


typedef struct PxrAnchorComponentBoxInfo{
    PxrStructureType    type;//PXR_TYPE_ANCHOR_COMPONENT_BOX_INFO
    PxrVector3f         center;
    PxrVector3f         extent;
} PxrAnchorBoxComponentInfo ;

typedef struct PxrAnchorComponentVolumeInfo{
    PxrStructureType    type;//PXR_TYPE_ANCHOR_COMPONENT_BOX_INFO
    PxrVector3f         center;
    PxrVector3f         extent;
} PxrAnchorComponentVolumeInfo ;

typedef struct PxrAnchorComponentRemoveInfo {
    uint64_t                anchor;
    PxrAnchorComponentType   compType;
} PxrAnchorComponentRemoveInfo ;
typedef uint64_t PxrAnchorComponentTypeFlags ;
static const PxrAnchorComponentTypeFlags PXR_ANCHOR_COMPONENT_TYPE_POSE_BIT_ = 0x00000001;
static const PxrAnchorComponentTypeFlags PXR_ANCHOR_COMPONENT_TYPE_PERSISTENCE_BIT_ = 0x00000002;
static const PxrAnchorComponentTypeFlags PXR_ANCHOR_COMPONENT_TYPE_SCENE_LABEL_BIT_ = 0x00000004;
static const PxrAnchorComponentTypeFlags PXR_ANCHOR_COMPONENT_TYPE_PLANE_BIT_ = 0x00000008;
static const PxrAnchorComponentTypeFlags PXR_ANCHOR_COMPONENT_TYPE_BOX_BIT_ = 0x00000010;
typedef struct PxrAnchorPlaneBoundaryInfo{
    PxrVector3f         center;
    PxrExtent2Df        extent;
} PxrAnchorPlaneBoundaryInfo ;
typedef struct PxrAnchorPlanePolygonInfo{
    uint32_t           polygonSizeCapacityInput;
    uint32_t           polygonSizeCountOutput;
    PxrVector3f*       polygonVertices;
} PxrAnchorPlanePolygonInfo ;
typedef struct PxrAnchorBoxInfo{
    PxrVector3f         center;
    PxrVector3f         extent;
} PxrAnchorBoxInfo ;

typedef struct PxrAnchorVolumeInfo{
    PxrVector3f         center;
    PxrVector3f         extent;
} PxrAnchorVolumeInfo ;

typedef struct PxrAnchorEntityList{
    uint32_t            count;
    uint64_t*   anchors; //handle
} PxrAnchorEntityList ;
typedef struct PxrAnchorEntityPersistInfo {
    PxrAnchorEntityList    anchorList; //handles
    PxrPersistLocation     location; //Persist location(local or remote)
} PxrAnchorEntityPersistInfo ;
typedef struct PxrEventDataAnchorEntityPersisted {
    PxrStructureType     type;//PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_PERSISTED
    PxrEventLevel eventLevel;
    uint64_t       taskId;
    PxrResult           result;
    PxrPersistLocation location; //Persist location(local or remote)
} PxrEventDataAnchorEntityPersisted ;
typedef struct PxrAnchorEntityUnpersistInfo{
    PxrAnchorEntityList    anchorList; //handles
    PxrPersistLocation     location; //Persist location(local or remote)
} PxrAnchorEntityUnpersistInfo ;
typedef struct PxrEventDataAnchorEntityUnpersisted {
    PxrStructureType     type;//PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_UNPERSISTED
    PxrEventLevel eventLevel;
    uint64_t       taskId;
    PxrResult            result;
    PxrPersistLocation location; //Persist location(local or remote)
} PxrEventDataAnchorEntityUnpersisted ;
typedef struct PxrAnchorEntityClearInfo{
    PxrPersistLocation location; //Persist location(local or remote)
} PxrAnchorEntityClearInfo ;
typedef struct PxrEventDataAnchorEntityCleared {
    PxrStructureType     type;//PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_CLEARED_
    PxrEventLevel eventLevel;
    uint64_t       taskId;
    PxrResult            result;
    PxrPersistLocation location; //Persist location(local or remote)
} PxrEventDataAnchorEntityCleared ;
typedef struct PxrEventDataAnchorEntityCreated {
    PxrStructureType     type;//PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_CREATED
    PxrEventLevel eventLevel;
    uint64_t       taskId;
    PxrResult            result;
    uint64_t            anchorHandle;
    PxrUuid  uuid;
} PxrEventDataAnchorEntityCreated ;
typedef struct PxrAnchorEntityLoadFilterBaseHeader {
    PxrStructureType    type;
    uint8_t            varying[128];
} PxrAnchorEntityLoadFilterBaseHeader ;
typedef struct PxrAnchorEntityLoadInfo{
    uint32_t                                    maxResult;
    int64_t                                  timeout;
    PxrPersistLocation                         location; //Persist location(local or remote)
    const PxrAnchorEntityLoadFilterBaseHeader* include;
    const PxrAnchorEntityLoadFilterBaseHeader* exclude;
} PxrAnchorEntityLoadInfo ;
typedef struct PxrAnchorEntityLoadUuidFilter{
    PxrStructureType    type;//PXR_TYPE_ANCHOR_ENTITY_LOAD_UUID_FILTER
    uint32_t           uuidCount;
    PxrUuid*         uuidList;
} PxrAnchorEntityLoadUuidFilter ;
typedef struct PxrAnchorEntityLoadComponentFilter{
    PxrStructureType              type;//PXR_TYPE_ANCHOR_ENTITY_LOAD_COMPONENT_FILTER_
    PxrAnchorComponentTypeFlags typeFlags;
} PxrAnchorEntityLoadComponentFilter ;
typedef struct PxrEventDataAnchorEntityLoaded{
    PxrStructureType     type;//PXR_TYPE_EVENT_DATA_ANCHOR_ENTITY_LOADED_
    PxrEventLevel eventLevel;
    uint64_t     taskId;
    PxrResult            result;
    uint32_t            count;
    PxrPersistLocation location; //Persist location(local or remote)
} PxrEventDataAnchorEntityLoaded ;
typedef struct PxrAnchorEntityLoadResult {
    uint64_t    anchor;
    PxrUuid           uuid;
} PxrAnchorEntityLoadResult ;
typedef struct PxrAnchorEntityLoadResults {
    uint32_t                    resultCapacityInput;
    uint32_t                    resultCountOutput;
    PxrAnchorEntityLoadResult*  results;
} PxrAnchorEntityLoadResults ;
typedef struct PxrEventDataSpatialSceneCaptured{
    PxrStructureType                type;//PXR_TYPE_EVENT_DATA_SPATIAL_SCENE_CAPTURED
    PxrEventLevel                   eventLevel;
    uint64_t                        taskId;
    PxrResult                       result;
    PxrSpatialSceneCaptureStatus    status;
} PxrEventDataSpatialSceneCaptured ;

typedef struct PxrEventSemiAutoRoomCaptureCandidatesUpdated {
    PxrStructureType            type;
    PxrEventLevel               eventLevel;
    uint32_t                    state;
    uint32_t                    count;
} PxrEventSemiAutoRoomCaptureCandidatesUpdated ;



//typedef struct PxrSemiAutoRoomCaptureCandidate {
//    float                       x;
//    float                       y;
//    float                       z;
//    float                       confidenceLevel;
//    float                       candidateID;
//    PxrRoomCaptureCandidateType type;
//} PxrSemiAutoRoomCaptureCandidate;


//typedef struct PxrSemiAutoRoomCaptureCandidateResults {
//    PxrSemiAutoRoomCaptureCandidate*    candidates;
//    int                                 candidates_num;
//}PxrSemiAutoRoomCaptureCandidateResults ;



/*
 *  scene capture sdk3.0
 */


typedef struct PxrSceneCaptureProviderCreateInfo {
    PxrStructureType          type;//PXR_TYPE_SCENE_CAPTURE_PROVIDER_CREATE_INFO
} PxrSceneCaptureProviderCreateInfo;

typedef struct PxrAutoSceneCaptureProviderCreateInfo {
    PxrStructureType          type;//PXR_TYPE_AUTO_SCENE_CAPTURE_PROVIDER_CREATE_INFO
} PxrAutoSceneCaptureProviderCreateInfo;

typedef struct PxrSemiAutoSceneCaptureProviderCreateInfo {
    PxrStructureType          type;//PXR_TYPE_SEMI_AUTO_SCENE_CAPTURE_PROVIDER_CREATE_INFO
} PxrSemiAutoSceneCaptureProviderCreateInfo;




// Still need this event to convey real-time messages reported by the algorithm.
typedef struct PxrEventAutoRoomCaptureUpdated {
    PxrStructureType             type; // PXR_TYPE_EVENT_AUTO_ROOM_CAPTURE_UPDATED
    PxrEventLevel                eventLevel;
    uint32_t                     state;
    uint32_t                     msg;
} PxrEventAutoRoomCaptureUpdated ;

typedef struct PxrEventDataSceneCaptured{
    PxrStructureType                type;//PXR_TYPE_EVENT_DATA_SPATIAL_SCENE_CAPTURED
    PxrEventLevel                   eventLevel;
    PxrResult                       result;
    PxrSceneCaptureStatus    status;
} PxrEventDataSceneCaptured ;

typedef struct PxrSceneCaptureStartCompletion {
    PxrStructureType          type;//PXR_TYPE_SCENE_CAPTURE_START_COMPLETION
    PxrResult                 futureResult;
} PxrSceneCaptureStartCompletion;


typedef enum PxrGeometryType {
    Plane = 0,
    Box,
} PxrGeometryType;

//deprecated
typedef struct PxrAutoSceneCaptureResult{
    uint32_t          id; // object id
    PxrPosef          pose;
    //uint64_t         space;//remove reference XrSpace
    // PxrTrackingOrigin pxrTrackingOrigin; // rm PxrTrackingOrigin
    PxrVector3f       center;
    PxrVector3f       extent;
    PxrSceneLabel   sceneLabel;
    PxrGeometryType geoType;
    uint32_t         polygonSize;
    PxrVector3f      polygonVertices[50];
} PxrAutoSceneCaptureResult;

typedef PxrAutoSceneCaptureResult PxrAutoSpatialSceneCaptureResult;

typedef struct PxrAutoSceneCaptureResults {
    PxrAutoSpatialSceneCaptureResult*    results;
    uint32_t resultCapacityInput;
    uint32_t resultCountOutput;
} PxrAutoSceneCaptureResults ;

typedef PxrAutoSceneCaptureResults PxrAutoSpatialSceneCaptureResults;


typedef struct PxrPoint3D {
    float                       x;
    float                       y;
    float                       z;
} PxrPoint3D;


typedef uint64_t PxrFlags64;
typedef PxrFlags64 PxrSpatialSceneDataTypeFlags;
// Flag bits for XrComponentTypeFlags
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_UNKNOWN_BIT_ = 0x00000001;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_FLOOR_BIT_ = 0x00000002;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_CEILING_BIT_ = 0x00000004;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_WALL_BIT_ = 0x00000008;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_DOOR_BIT_ = 0x00000010;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_WINDOW_BIT_ = 0x00000020;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_OPENING_BIT_ = 0x00000040;
static const PxrSpatialSceneDataTypeFlags PXR_SPATIAL_SCENE_DATA_TYPE_OBJECT_BIT_ = 0x00000080;

typedef struct PxrAnchorEntityLoadSpatialSceneFilter{
    PxrStructureType               type;//PXR_TYPE_ANCHOR_ENTITY_LOAD_SPATIAL_SCENE_FILTER_
    PxrSpatialSceneDataTypeFlags typeFlags;
} PxrAnchorEntityLoadSpatialSceneFilter ;


#define ROOM_SCENE_DATA_UPDATE_PER_SIZE 128
typedef struct PxrEventRoomSceneDataUpdateResult
{
    PxrStructureType type;
    PxrEventLevel eventLevel;
    PxrSpatialInstanceUuid anchorUuid;
    uint64_t roomSceneDataHandle;
    uint32_t result;
    uint32_t dataLen;
    uint8_t roomSceneData[ROOM_SCENE_DATA_UPDATE_PER_SIZE];
} PxrEventRoomSceneDataUpdateResult;

typedef enum PxrMrFeature {
    PXR_MR_FEATURE_RC_LINE_CALIBRATION = 1 >> 0,
    PXR_MR_FEATURE_MRX = 0x7FFFFFFF
} PxrMrFeature;


//spatial mesh begin
// Provided by XR_PICO_spatial_mesh
typedef uint64_t PxrSpaceLocationFlags;
// Flag bits for XrSpaceLocationFlags
static const PxrSpaceLocationFlags PXR_SPACE_LOCATION_ORIENTATION_VALID_BIT = 0x00000001;
static const PxrSpaceLocationFlags PXR_SPACE_LOCATION_POSITION_VALID_BIT = 0x00000002;
static const PxrSpaceLocationFlags PXR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT = 0x00000004;
static const PxrSpaceLocationFlags PXR_SPACE_LOCATION_POSITION_TRACKED_BIT = 0x00000008;


typedef struct PxrSpaceLocation {
    PxrStructureType         type; //PXR_TYPE_SPACE_LOCATION
    PxrSpaceLocationFlags    locationFlags;
    PxrPosef                 pose;
} PxrSpaceLocation;
//spatial mesh end


typedef enum PxrFeaturePointsQualityLevel {
    PXR_NOT_AVAILABLE,
    PXR_INSUFFICIENT,
    PXR_NEED_MORE_SCAN,
    PXR_COMPLETE
} PxrFeaturePointsQualityLevel;


/*
 * mr sdk 3.0
 */
typedef struct PxrSenseDataProviderCreateInfoBaseHeader{
    PxrStructureType          type;
    uint8_t                   vary[128];
} PxrSenseDataProviderCreateInfoBaseHeader;


typedef enum PxrSpatialMeshLod{
    PXR_MESH_LEVEL_LOW= 0,
    PXR_MESH_LEVEL_MEDIUM= 1,
    PXR_MESH_LEVEL_HIGH= 2
} PxrSpatialMeshLod;

typedef uint64_t PxrSpatialMeshConfigFlags;
static const PxrSpatialMeshConfigFlags PXR_SPATIAL_MESH_CONFIG_SEMANTIC_BIT = 0x00000001;
static const PxrSpatialMeshConfigFlags PXR_SPATIAL_MESH_CONFIG_SEMANTIC_ALIGN_WITH_VERTEX_BIT = 0x00000002;


typedef struct PxrSpatialMeshProviderCreateInfo{
    PxrStructureType          type;//PXR_TYPE_SPATIAL_MESH_PROVIDER_CREATE_INFO
    PxrSpatialMeshConfigFlags    configFlags;//Bitmask of the configuration. e.g. semantic, normal, LOD..
    PxrSpatialMeshLod    lod;//TBC: LOD level of mesh
} PxrSpatialMeshProviderCreateInfo;

typedef struct PxrSenseDataProviderStartCompletion{
    PxrStructureType         type;//PXR_TYPE_SENSE_DATA_PROVIDER_START_COMPLETION
    PxrResult                futureResult;
} PxrSenseDataProviderStartCompletion;

typedef struct PxrUnpersistAnchorByUUIDCompletion{
    PxrStructureType         type;//PXR_TYPE_SENSE_UNPERSIST_ANCHOR_BY_UUID_COMPLETION
    PxrResult                futureResult;
} PxrUnpersistAnchorByUUIDCompletion;

typedef struct PxrFuturePollInfoEXT {
    PxrStructureType          type; //PXR_TYPE_FUTURE_POLL_INFO_EXT
    uint64_t                  future;
} PxrFuturePollInfoEXT;

typedef enum PxrFutureStateEXT {
    PXR_FUTURE_STATE_PENDING_EXT = 1,
    PXR_FUTURE_STATE_READY_EXT = 2,
    PXR_FUTURE_STATE_MAX_ENUM_EXT = 0x7FFFFFFF
} PxrFutureStateEXT;

typedef struct PxrFuturePollResultEXT {
    PxrStructureType           type; //PXR_TYPE_FUTURE_POLL_RESULT_EXT
//    void*                      next;
    PxrFutureStateEXT          state;
} PxrFuturePollResultEXT;

typedef struct PxrFuturePollResultAndProgress {
    PxrStructureType           type; //PXR_TYPE_FUTURE_POLL_RESULT_EXT
    PxrFutureStateEXT          state;
    int32_t                    progress;
} PxrFuturePollResultAndProgress;

typedef enum PxrSenseDataProviderState {
    PXR_SENSE_DATA_PROVIDER_STATE_INITIALIZED = 0,
    PXR_SENSE_DATA_PROVIDER_STATE_RUNNING = 1,
    PXR_SENSE_DATA_PROVIDER_STATE_STOPPED = 2,
    PXR_SENSE_DATA_PROVIDER_STATE_MAX_ENUM = 0x7FFFFFFF
} PxrSenseDataProviderState;

typedef struct PxrSpatialEntityUuidFilter{
    PxrStructureType            type;//PXR_TYPE_SPATIAL_ENTITY_UUID_FILTER
    uint32_t                    uuidCount;
    PxrUuid*                    uuidList;
} PxrSpatialEntityUuidFilter;

typedef enum PxrSemanticLabel{
    PXR_SEMANTIC_LABEL_UNKNOWN = 0,
    PXR_SEMANTIC_LABEL_FLOOR = 1,
    PXR_SEMANTIC_LABEL_CEILING = 2,
    PXR_SEMANTIC_LABEL_WALL = 3,
    PXR_SEMANTIC_LABEL_DOOR = 4,
    PXR_SEMANTIC_LABEL_WINDOW = 5,
    PXR_SEMANTIC_LABEL_OPENING = 6,
    PXR_SEMANTIC_LABEL_TABLE = 7,
    PXR_SEMANTIC_LABEL_SOFA = 8,
    PXR_SEMANTIC_LABEL_CHAIR = 9,
    PXR_SEMANTIC_LABEL_HUMAN = 10,
    PXR_SEMANTIC_LABEL_BEAM = 11,
    PXR_SEMANTIC_LABEL_COLUMN = 12,
    PXR_SEMANTIC_LABEL_CURTAIN = 13,
    PXR_SEMANTIC_LABEL_CABINET = 14,
    PXR_SEMANTIC_LABEL_BED = 15,
    PXR_SEMANTIC_LABEL_PLANT = 16,
    PXR_SEMANTIC_LABEL_SCREEN = 17,
    PXR_SEMANTIC_LABEL_VIRTUAL_WALL = 18,
    PXR_SEMANTIC_LABEL_REFRIGERATOR = 19,
    PXR_SEMANTIC_LABEL_WASHING_MACHINE = 20,
    PXR_SEMANTIC_LABEL_AIR_CONDITIONER = 21,
    PXR_SEMANTIC_LABEL_LAMP = 22,
    PXR_SEMANTIC_LABEL_WALL_ART = 23,
    PXR_SEMANTIC_LABEL_STAIRWAY_PICO = 24,
} PxrSemanticLabel;

// Semantic filter
typedef struct PxrSpatialEntitySemanticFilter{
    PxrStructureType             type;//PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_FILTER
    uint32_t                     semanticCount;
    PxrSemanticLabel*          semantics;
} PxrSpatialEntitySemanticFilter;

typedef struct PxrSenseDataQueryFilterBaseHeader{
    PxrStructureType             type;
    uint8_t                      vary[128];
} PxrSenseDataQueryFilterBaseHeader;

typedef struct PxrSenseDataQueryInfo{
    PxrStructureType             type;// PXR_TYPE_SENSE_DATA_QUERY_INFO
    PxrSenseDataQueryFilterBaseHeader*    filter;//extent in other feature extensions
} PxrSenseDataQueryInfo;

typedef struct PxrSenseDataQueryCompletion{
    PxrStructureType            type;//PXR_TYPE_SENSE_DATA_QUERY_COMPLETION
    PxrResult                   futureResult;
    uint64_t                    snapshotHandle;//query result in a snapshot
} PxrSenseDataQueryCompletion;

typedef struct PxrQueriedSpatialEntityInfo{
    PxrStructureType            type; //PXR_TYPE_SENSE_DATA_QUERY_INFO
    uint64_t                    spatialEntity;
    uint64_t                    time;// spatial entity 
    PxrUuid                     uuid;
} PxrQueriedSpatialEntityInfo;

typedef struct PxrQueriedSenseDataGetInfo {
    PxrStructureType          type;// PXR_TYPE_QUERIED_SENSE_DATA_GET_INFO
    uint64_t                  snapshotHandle;
} PxrQueriedSenseDataGetInfo;

typedef struct PxrQueriedSenseData{
    PxrStructureType            type; //PXR_TYPE_QUERIED_SENSE_DATA
    uint32_t                    queriedSpatialEntityCapacityInput;
    uint32_t                    queriedSpatialEntityCountOutput;
    PxrQueriedSpatialEntityInfo*   queriedSpatialEntities;
} PxrQueriedSenseData;

typedef struct PxrSpatialEntityComponentTypes{
    PxrStructureType                    type;
    uint32_t                           componentTypeCapacityInput;
    uint32_t                           componentTypeCountOutput;
    PxrSpatialEntityComponentType*    componentTypes;
} PxrSpatialEntityComponentTypes;

// Base header struct of get info
typedef struct PxrSpatialEntityComponentInfoGetInfoBaseHeader{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntityComponentInfoGetInfoBaseHeader;

// Base header struct of component info
typedef struct PxrSpatialEntityComponentInfoBaseHeader{
    PxrStructureType         type;
    uint8_t                  varying[128];
} PxrSpatialEntityComponentInfoBaseHeader;

typedef struct PxrSpatialEntityComponentSetInfo {
    PxrStructureType                    type;//PXR_TYPE_SPATIAL_ENTITY_COMPONENT_SET_INFO
    PxrSpatialEntityComponentType     componentType;
    PxrSpatialEntityComponentInfoBaseHeader* componentInfo;
} PxrSpatialEntityComponentSetInfo;

//-----Structs derived from base header---//
// Location
typedef struct PxrSpatialEntityLocationGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
    uint64_t                           baseSpace;
    uint64_t                           time;
} PxrSpatialEntityLocationGetInfo;

typedef struct PxrSpatialEntityLocationInfo{
    PxrStructureType         type;
    PxrSpaceLocationFlags    locationFlags;
    PxrPosef                 pose;
} PxrSpatialEntityLocationInfo;

// Semantic
typedef struct PxrSpatialEntitySemanticGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntitySemanticGetInfo;

typedef struct PxrSpatialEntitySemanticInfo{
    PxrStructureType         type; // PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_INFO
    uint32_t                 semanticCapacityInput;
    uint32_t                 semanticCountOutput;
    PxrSemanticLabel*      semanticLabels;
} PxrSpatialEntitySemanticInfo;

typedef struct PxrSpatialEntitySemanticComponentInfo {
    PxrStructureType          type; // PXR_TYPE_SPATIAL_ENTITY_SEMANTIC_COMPONENT_INFO
    PxrSemanticLabel        semanticLabel;
}PxrSpatialEntitySemanticComponentInfo;


//BoundingBox 2D
typedef struct PxrSpatialEntityBoundingBox2DGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntityBoundingBox2DGetInfo;

typedef struct PxrSpatialEntityAutoSceneCaptureResultGetInfo {
    PxrStructureType                   type; // PXR_TYPE_SPATIAL_ENTITY_AUTO_SCENE_CAPTURE_RESULT_GET_INFO
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType; // PXR_SPATIAL_ENTITY_COMPONENT_TYPE_AUTO_SCENE_CAPTURE_RESULT
} PxrSpatialEntityAutoSceneCaptureResultGetInfo;

typedef struct PxrOffset2Df {
    float    x;
    float    y;
} PxrOffset2Df;

typedef struct PxrRect2Df {
    PxrOffset2Df    offset;
    PxrExtent2Df    extent;
} PxrRect2Df;

typedef struct PxrSpatialEntityBoundingBox2DInfo{
    PxrStructureType          type; // PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_2D_INFO
    PxrRect2Df                boundingBox2D;
} PxrSpatialEntityBoundingBox2DInfo;


typedef struct PxrSpatialEntityBoundingBox2DComponentInfo {
    PxrStructureType          type; // PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_2D_COMPONENT_INFO
    PxrRect2Df                boundingBox2D;
}PxrSpatialEntityBoundingBox2DComponentInfo;


// Polygon
typedef struct PxrSpatialEntityPolygonGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntityPolygonGetInfo;

typedef struct PxrSpatialEntityPolygonInfo{
    PxrStructureType          type;  // PXR_TYPE_SPATIAL_ENTITY_POLYGON_INFO
    uint32_t                  polygonCapacityInput;
    uint32_t                  polygonCountOutput;
    PxrVector2f*              polygonVertices;
} PxrSpatialEntityPolygonInfo;


typedef struct PxrSpatialEntityPolygonComponentInfo{
    PxrStructureType          type; // PXR_TYPE_SPATIAL_ENTITY_POLYGON_COMPONENT_INFO
    uint32_t                  vertexCount;
    PxrVector2f*              vertices;
}PxrSpatialEntityPolygonComponentInfo;

// BoundingBox 3D
typedef struct PxrSpatialEntityBoundingBox3DGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntityBoundingBox3DGetInfo;

typedef struct PxrExtent3Df {
    float    width;
    float    height;
    float    depth;
} PxrExtent3Df;

typedef struct PxrBoxf{
    PxrPosef              center;
    PxrExtent3Df          extents;
} PxrBoxf;

typedef struct PxrSpatialEntityBoundingBox3DInfo{
    PxrStructureType          type; //PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_3D_INFO
    PxrBoxf                   boundingBox3D;
} PxrSpatialEntityBoundingBox3DInfo;


typedef struct PxrSpatialEntityBoundingBox3DComponentInfo {
    PxrStructureType          type; // PXR_TYPE_SPATIAL_ENTITY_BOUNDING_BOX_3D_COMPONENT_INFO
    PxrBoxf                   boundingBox3D;
}PxrSpatialEntityBoundingBox3DComponentInfo;

// Triangle mesh
typedef struct PxrSpatialEntityTriangleMeshGetInfo{
    PxrStructureType                   type;
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType;
} PxrSpatialEntityTriangleMeshGetInfo;

typedef struct PxrSpatialEntityTriangleMeshInfo{
    PxrStructureType      type;//XR_TRIANGLE_MESH
    uint32_t              vertexCapacityInput;
    uint32_t              vertexCountOutput;
    PxrVector3f*          vertices;
    uint32_t              indexCapacityInput;
    uint32_t              indexCountOutput;
    uint16_t*             indices;
} PxrSpatialEntityTriangleMeshInfo;

typedef struct PxrEventDataSenseDataProviderStateChanged{
    PxrStructureType              type;//PXR_TYPE_EVENT_DATA_SENSE_DATA_PROVIDER_STATE_CHANGED
    uint64_t                      provider; //provider handle
    PxrSenseDataProviderState   newState;
} PxrEventDataSenseDataProviderStateChanged;

typedef struct PxrEventDataSenseDataUpdated{
    PxrStructureType              type;//PXR_TYPE_EVENT_DATA_SENSE_DATA_UPDATED
    uint64_t                      provider; //provider handle
} PxrEventDataSenseDataUpdated;

typedef struct PxrSpatialEntityAnchorRetrieveInfo{
    PxrStructureType         type;//PXR_TYPE_SPATIAL_ENTITY_ANCHOR_RETRIEVE_INFO
    uint64_t                 spatialEntity;
} PxrSpatialEntityAnchorRetrieveInfo;

typedef struct PxrAnchorLocateInfo{
    PxrStructureType        type; // PXR_TYPE_ANCHOR_LOCATION_INFO
    PxrTrackingOrigin       baseSpace;
    uint64_t                time;
    uint64_t                anchor;
} PxrAnchorLocateInfo;

typedef struct PxrSpatialAnchorProviderCreateInfo{
    PxrStructureType          type; //PXR_TYPE_SPATIAL_ANCHOR_PROVIDER_CREATE_INFO
} PxrSpatialAnchorProviderCreateInfo;

typedef struct PxrSpatialAnchorCreateInfo {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_CREATE_INFO
    PxrTrackingOrigin         baseSpace;
    PxrPosef                  pose;
    double                    timeMs;//Timestamp in milliseconds
} PxrSpatialAnchorCreateInfo;


typedef struct PxrSpatialAnchorCreateCompletion {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_CREATE_COMPLETION
    PxrResult                 futureResult;
    uint64_t                  anchor;
    PxrUuid                   uuid;
} PxrSpatialAnchorCreateCompletion;

typedef struct PxrSpatialAnchorPersistInfo {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_PERSIST_INFO
    PxrPersistenceLocation   location;
    uint64_t                  anchor;
} PxrSpatialAnchorPersistInfo;

typedef struct PxrSpatialAnchorPersistCompletion {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_PERSIST_COMPLETION
    PxrResult                 futureResult;
    uint64_t                  anchor;
    PxrUuid                   uuid;
} PxrSpatialAnchorPersistCompletion;

typedef struct PxrSpatialAnchorUnpersistInfo {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_UNPERSIST_INFO
    uint64_t                  anchor;
} PxrSpatialAnchorUnpersistInfo;

typedef struct PxrSpatialAnchorUnpersistCompletion {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_UNPERSIST_COMPLETION
    PxrResult                 futureResult;
    uint64_t                  anchor;
    PxrUuid                   uuid;
} PxrSpatialAnchorUnpersistCompletion;

/*
 * mr sdk 3.0 cloud anchor
 */
typedef struct PxrSpatialAnchorShareInfo {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_SHARE_INFO
    uint64_t                  anchor;
} PxrSpatialAnchorShareInfo;

typedef struct PxrSpatialAnchorShareCompletion {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_SHARE_COMPLETION
    PxrResult                 futureResult;
} PxrSpatialAnchorShareCompletion;

typedef struct PxrSharedSpatialAnchorDownloadInfo {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_DOWNLOAD_INFO
    PxrUuid                   uuid;
} PxrSharedSpatialAnchorDownloadInfo;

typedef struct PxrSharedSpatialAnchorDownloadCompletion {
    PxrStructureType          type;//PXR_TYPE_SPATIAL_ANCHOR_DOWNLOAD_COMPLETION
    PxrResult                 futureResult;
} PxrSharedSpatialAnchorDownloadCompletion;

//deprecated
typedef struct PxrSenseDataSemiAutoSceneCaptureResult {
    PxrStructureType                        type;//PXR_TYPE_SEMI_AUTO_SCENE_CAPTURE_RESULT
    uint64_t                               resultHandle;
    uint64_t                               updateTime;
    float                                  x;
    float                                  y;
    float                                  z;
    float                                  confidenceLevel;
    PxrSemiAutoRoomCaptureCandidateType   candidateType;
} PxrSenseDataSemiAutoSceneCaptureResult;


// auto scene capture 

// XrSenseDataAutoSceneCaptureResultPICO
typedef struct PxrSenseDataAutoSceneCaptureResult {
    PxrStructureType  type; // PXR_TYPE_SCENE_DATA_AUTO_SCENE_CAPTURE_RESULT
    uint32_t          id; // object id
    PxrPosef          pose;
    PxrVector3f       center;
    PxrVector3f       extent;
    PxrSemanticLabel   semanticLabel;
    PxrGeometryType   geoType;
    char              edge_completion_flags;
    uint32_t          polygonSize;
    PxrVector3f       polygonVertices[50];
}PxrSenseDataAutoSceneCaptureResult;

typedef PxrSenseDataAutoSceneCaptureResult PxrSpatialEntityAutoSceneCaptureResultInfo;

// auto scene capture
typedef struct PxrSpatialEntitySemiAutoSceneCaptureResultsGetInfo{
    PxrStructureType                   type; // PXR_TYPE_SPATIAL_ENTITY_SEMI_AUTO_SCENE_CAPTURE_RESULT_GET_INFO
    uint64_t                           entity;
    PxrSpatialEntityComponentType    componentType; //PXR_SPATIAL_ENTITY_COMPONENT_TYPE_SEMI_AUTO_SCENE_CAPTURE_RESULT
} PxrSpatialEntitySemiAutoSceneCaptureResultsGetInfo;
typedef struct PxrSpatialEntitySemiAutoSceneCaptureResultInfo {
    PxrStructureType                        type; // PXR_TYPE_SPATIAL_ENTITY_SEMI_AUTO_SCENE_CAPTURE_RESULT_INFO
    uint64_t                                updateTime;
    float                                   x;
    float                                   y;
    float                                   z;
    float                                   confidenceLevel;
    uint32_t                                candidateID;
    PxrSemiAutoRoomCaptureCandidateType   candidateType;
}PxrSpatialEntitySemiAutoSceneCaptureResultInfo;

typedef enum PxrSpatialMapMemLimitedReason {
    MapMemoryLimitedUnknown = 0,
    MapCountLimited,
    SingleMapMemoryLimited,
    TotalMapMemoryLimited,
} XrSpatialMapMemLimitedReason;

typedef struct PxrEventDataSpatialMapMemLimited{
    PxrStructureType type;//PXR_TYPE_EVENT_DATA_SPATIAL_MAP_MEM_LIMITED
    PxrEventLevel eventLevel;
    PxrSpatialMapMemLimitedReason reason;
}PxrEventDataSpatialMapMemLimited;

#endif  // PXR_TYPES_H