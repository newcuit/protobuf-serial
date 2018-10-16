#ifndef IAV2HP_HIRAIN_H
#define IAV2HP_HIRAIN_H

#include <stdlib.h>   // size_t

#ifdef __cplusplus
extern "C" {
#endif

// return result define
#define IAV2HP_SUCCESS                  ( 0 )   ///< 成功
#define IAV2HP_INITIALIZATION_FAILED    ( 1 )   ///< 初始化失败
#define IAV2HP_UNINITIALIZED            ( 2 )   ///< 未初始化
#define IAV2HP_RUNNING_HP_FAILED        ( 3 )   ///< 运行失败
#define IAV2HP_RUNNING_VP_FAILED        ( 4 )   ///< VP启动失败
#define IAV2HP_PARAM_ERROR              ( 5 )   ///< 参数错误

#define MAX_NAME_SIZE                   ( 64 )

typedef int    av2hp_e;

/**  av2hp运行过程中错误码 */
typedef enum av2hp_errorCode
{
    av2hp_errorCode_noError = 0,
    av2hp_errorCode_loadMapDbFiled,
    av2hp_errorCode_loadAdasDataFiled,
    av2hp_errorCode_engineInitFiled,
    av2hp_errorCode_canInitFiled,
    av2hp_errorCode_internalError,
    // reserve
    av2hp_errorCode_socketInitFiled
}av2hp_errorCode;

typedef struct av2hp_point
{
    unsigned int x;
    unsigned int y;
} av2hp_point;

typedef struct av2hp_car
{
    int mode;
    av2hp_point         point;
    short               carOri;
    int                 speed;
    unsigned long long  segmId;
    unsigned int        disBackward;
} av2hp_car;

typedef struct av2hp_meta
{
    int                 drivingSide;
    int                 speedUnits;
    char                mapProvider[MAX_NAME_SIZE];
    char                mapVersion[MAX_NAME_SIZE];
} av2hp_meta;


/**  set gps info */
typedef struct av2hp_satellite
{
    short               satId;              ///< satellite id
    short               elevation;          ///< [0, 90]  degree
    short               azimuth;            ///< [0, 359] degree
    short               SNRatio;            ///< [0, 99]  dB
} av2hp_satellite;

typedef struct av2hp_dateTime
{
    short               m_hours;
    short               m_minutes;
    short               m_seconds;

    short               m_year;
    short               m_month;
    short               m_day;
} av2hp_dateTime;

typedef enum av2hp_coordinateKind
{
    av2hp_coordinate_WGS84 = 0,
    av2hp_coordinate_GCJ02
} av2hp_coordinateKind;

typedef enum av2hp_speedUnit
{
    av2hp_speedUnit_ms = 0,                 ///< 米/秒
    av2hp_speedUnit_knot                    ///< 节
} av2hp_speedUnit;


typedef struct av2hp_coordinate
{
    av2hp_coordinateKind m_kind;            ///< WGS坐标系或者02坐标系

    double              m_lon;
    double              m_lat;
    double              m_alt;
} av2hp_coordinate;

typedef struct av2hp_speed
{
    av2hp_speedUnit     m_unit;             ///< speed单位: 节 or 米/秒

    double              m_value;            ///< speed value
} av2hp_speed;

typedef struct av2hp_gpsInfo
{
    short               m_valid;            ///< 1: gps fixtype:2D/3D, 0 : unknown
    av2hp_dateTime      m_dateTime;
    long long           m_timestamp;        ///< in milli-seconds
    av2hp_coordinate    m_pos;
    double              m_orient;           ///< [0.0 ~ 360.0) GPS Original angle
    av2hp_speed         m_speed;            ///< WARNING: It's fixpt, not an integer. If you want to 
                                            ///< convert a float point number into fixpt, multiply it by (1 << FIXPT_SHIFT)
                                            // The unit is m/s.
    short               m_gpsQuality;       ///< not used at presents, 1: good gps singal; 0: bad gps signal.
    int                 m_hdop;             ///< Horizontal Dilution Of Precision
    int                 m_pdop;             ///< Position Dilution Of Precision
    int                 m_vdop;             ///< Vertical Dilution Of Precision

    int                 m_satInViewNum;     ///< 当前收到的GPS卫星个数
    int                 m_satNum;           ///< 星图卫星数。也就是GPGSV中，含有有效信噪比的卫星数。的数量。
                                            ///< 遍历m_satellites数组时要用它。satInViewNum != satNum。但一般差不多。
    av2hp_satellite     m_satellites[20];   ///< 将原始的卫星详细数据放在这里就可以，不用过滤。

} av2hp_gpsInfo;


/**
*	@brief 初始化接口函数, configure文件可放在exe同级目录（也可以配置到其他目录），地图数据及adas数据的绝对目录部分和.conf相同配置。
*   @note 此函数通常在系统初期化时调用
*/
av2hp_e Av2HP_init(const char *conf_path);

/** 
*	@brief 路网消息推送回调函数 
*/
typedef av2hp_e(*Av2HP_pushMessageCB)(const char *message, int *message_num);

/**
*	@brief 设置路网消息推送回调接口函数
*   @note 此函数通常在系统初期化时调用
*/
av2hp_e Av2HP_setMessageCB(Av2HP_pushMessageCB av2hp_cb);

/**
*	@brief 此函数通常在系统初期化时调用或根据需要进行。
*   @note 此函数通常在系统初期化时调用
*/
av2hp_e Av2HP_getMeta(av2hp_meta *meta_data);

/**
*	@brief 获取消息类型
*
*/
unsigned char Av2HP_getMsgType(unsigned char* value, unsigned char length);

/**
*	@brief 设置重发标志位
*
*/
void Av2HP_setRetransmission(unsigned char* value, unsigned char length);

/**
*    @brief 获取STUB消息是否包含路网重构标志
*/
unsigned char Av2HP_getReconstFlag(unsigned char* value, unsigned char length);


/**
*	@brief 开始运行接口函数
*/
av2hp_e Av2HP_run();

/**
*	@brief 预留接口，未实现, 暂时pause av2hp的运行。
*/
av2hp_e Av2HP_stop();

/**
*	@brief 发送车辆信息接口函数（搭载导航系统的场合使用）
*/
av2hp_e Av2HP_setCarInfo(av2hp_car *car);

/**
*	@brief 设定gps信号
*/
av2hp_e Av2HP_setGpsInfo(av2hp_gpsInfo *gps);

/**
*	@brief 设定导航的径路（搭载导航系统的场合使用）
*/
av2hp_e Av2HP_route(unsigned long long* DSegIDs, size_t num);

/**
*	@brief 退出接口函数
*	@note 此函数通常在系统退出的时候调用
*/
av2hp_e Av2HP_destory();


/**
*	@brief 调试用接口
*	@note  如果发生了error，可以调用此函数，取得错误发生原因。
*/
av2hp_errorCode Av2HP_getLastError();

/**
*	@brief 初始化接口函数, 会启动simulator, 主要用于debug或演示
*/
av2hp_e Av2HP_init_withSimulator(const char *conf_path);


#ifdef __cplusplus
}
#endif

#endif
