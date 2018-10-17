#include "id.h"
#include "gps.h"
#include "pbserial.h"
#include "iav2hp.h"
#include "minmea.h"
#include <time.h>
#include <memory.h>
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
* Description    : 定义电子地图消息类型
**************************************************************************************/
#define AV2_MSG_TYPE_SYSTEM_SPECIFIC    (0)
#define AV2_MSG_TYPE_POSITION           (1)
#define AV2_MSG_TYPE_SEGMENT            (2)
#define AV2_MSG_TYPE_STUB               (3)
#define AV2_MSG_TYPE_PROFILE_SHORT      (4)
#define AV2_MSG_TYPE_PROFILE_lONG       (5)
#define AV2_MSG_TYPE_META_DATA          (6)
#define AV2_MSG_TYPE_RESERVED           (7)
#define MAX_CAN_SIZE                    200
#define MAX_CONTEXT_SIZE                100

/**************************************************************************************
* Description    : 定义电子地图需要的结构和配置
**************************************************************************************/
typedef struct data_rain {
	uint8_t m_buffer[8];
}emaps_data;

static int transport_fd = -1;
static const char *config_file = "/etc/config/AV2HP.conf";

/************************************************************************************** * 
 * * FunctionName   : get_gpsinfo()
 * * Description    : GPS数据解析成电子地图需要的格式
 * * EntryParameter : info,电子地图GPS数据结构, data,指向gps数据,len，指向数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int get_gpsinfo(av2hp_gpsInfo *info, char *data, int len)
{
	int i;
	struct gpsinfo gps = {0};

	gpsbuffer_to_gpsinfo(&gps, data, len);

	info->m_valid              = gps.rmc.valid;
	info->m_dateTime.m_hours   = gps.rmc.time.hours;
	info->m_dateTime.m_minutes = gps.rmc.time.minutes;
	info->m_dateTime.m_seconds = gps.rmc.time.seconds;
	info->m_dateTime.m_year    = gps.rmc.date.year+2000;
	info->m_dateTime.m_month   = gps.rmc.date.month;
	info->m_dateTime.m_day     = gps.rmc.date.day;
	info->m_timestamp          = 0;
	info->m_pos.m_kind         = av2hp_coordinate_WGS84;
	info->m_pos.m_lon          = minmea_tocoord(&gps.rmc.longitude);
	info->m_pos.m_lat          = minmea_tocoord(&gps.rmc.latitude);
	info->m_pos.m_alt          = minmea_tofloat(&gps.gga.altitude);
	info->m_speed.m_unit       = av2hp_speedUnit_ms;
	info->m_speed.m_value      = minmea_tofloat(&gps.vtg.speed_kph)/3.6;
	info->m_orient             = minmea_tofloat(&gps.rmc.course);
	info->m_hdop               = minmea_tofloat(&gps.gsa.hdop);
	info->m_pdop               = minmea_tofloat(&gps.gsa.pdop);
	info->m_vdop               = minmea_tofloat(&gps.gsa.vdop);

	info->m_gpsQuality = 1;
	info->m_satInViewNum       = gps.gsv[0].total_sats;
	info->m_satNum             = 0;
	for (i = 0; i < 12; i++){
		if (gps.gsa.sats[i] > 0) info->m_satNum++;
	}

	return 0;
}

/*************************************************************************************** 
 * * FunctionName   : emaps_can_send()
 * * Description    : 电子地图数据发送
 * * EntryParameter : canid,can数据id, data,指向电子地图CAN数据,len，指向数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int emaps_can_send(int canid, char *data, int len)
{
	Can message = CAN__INIT;
	Subid msg  = SUBID__INIT;
	ProtobufCBinaryData pdata;
	char buffer[MAX_CAN_SIZE], oob[MAX_CONTEXT_SIZE];

	// 1.初始化Protobuf数据
	msg.id = IOC__DATA;
	msg.n_subdata = 1;
	msg.subdata = &pdata;

	// 2.打包CAN数据
	message.id = canid;
	message.data.len = len;
	message.data.data = data;

	// 3.打包子ID
	msg.subdata->len = can__get_packed_size(&message);
	msg.subdata->data = (uint8_t *)oob;

	// 4.打包获取的数据
	can__pack(&message, msg.subdata->data);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	return packages_send(transport_fd, EMAPS_ID, buffer, subid__get_packed_size(&msg));
}

/**************************************************************************************
 * * FunctionName   : emaps_callback()
 * * Description    : 电子地图回调函数
 * * EntryParameter : message,指向电子地图处理的数据， n,指向数据的个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int emaps_callback(const char *message, int *n)
{
	size_t i = 0;
	uint8_t type;
	emaps_data *p = (emaps_data *)message;

	if(message == NULL || n == NULL) {
		syslog(LOG_ERR,"%s message failed!!!\n", __func__);
		return 0;
	}

	for(i = 0; i < *n; i++) {
		type = Av2HP_getMsgType(p->m_buffer, 8);
		switch(type) {
		case AV2_MSG_TYPE_POSITION:
			emaps_can_send(0x18F0F69F, p->m_buffer, 8);
			break;
		case AV2_MSG_TYPE_SEGMENT:
			break;
		case AV2_MSG_TYPE_STUB:
			emaps_can_send(0x18F0F69F, p->m_buffer, 8);
			//syslog(LOG_ERR,"%d\n", Av2HP_getReconstFlag(p->m_buffer, 8));
			break;
		case AV2_MSG_TYPE_PROFILE_SHORT:
			emaps_can_send(0x18F0F69F, p->m_buffer, 8);
			break;
		case AV2_MSG_TYPE_PROFILE_lONG:
			break;
		defalut:
			syslog(LOG_ERR,"unknown message received\n");
			break;
		}
		p = p + 1;
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : handle_emaps_data()
 * * Description    : 处理GPS数据
 * * EntryParameter : data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_emaps_data(ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Gps *gps = NULL;
	av2hp_gpsInfo gpsinfo = {0};

	for (i = 0; i < n_data; i++) {
		gps = gps__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(gps == NULL || gps->nmea.data == NULL)) continue;

		get_gpsinfo(&gpsinfo, gps->nmea.data, gps->nmea.len);
		Av2HP_setGpsInfo(&gpsinfo);
		gps__free_unpacked(gps, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : emaps_handler()
 * * Description    : emaps数据处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int emaps_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__DATA: handle_emaps_data(msg->subdata, msg->n_subdata);
	break;
	}
	subid__free_unpacked(msg, NULL);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : emaps_init()
 * * Description    : emaps初始化
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int emaps_init(int fd)
{
	av2hp_meta meta_data;

	// 1.初始化电子地图
	if(Av2HP_init(config_file) != IAV2HP_SUCCESS) {
		syslog(LOG_ERR,"av2hp init(%s) failed!!!\n", config_file);
		return -1;
	}
	// 2.获取Meta信息
	Av2HP_getMeta(&meta_data);

	// 3.设置地图获取数据回调
	Av2HP_setMessageCB(emaps_callback);

	// 4.运行电子地图
	if(Av2HP_run() != IAV2HP_SUCCESS) {
		syslog(LOG_ERR,"av2hp run(%s) failed!!!\n", config_file);
		return -1;
	}
	transport_fd = fd;
}

/**************************************************************************************
 * * FunctionName   : emaps_deinit()
 * * Description    : emaps解初始化
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int emaps_deinit(int fd)
{
	if(transport_fd >= 0) {
		Av2HP_destory();
	}
	transport_fd = -1;
	return  0;
}

// 注册ID
register_id(EMAPS_ID, emaps_init, emaps_deinit, emaps_handler);
