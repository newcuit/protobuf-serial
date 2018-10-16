#include "id.h"
#include "pbserial.h"
#include <protobuf-c/data.pb-c.h>

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

	for (i=0; i<n_data; i++) {
		gps = gps__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(gps == NULL || gps->nmea.data == NULL)) continue;

		// Fix me, emaps package
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

// 注册ID
register_id(EMAPS_ID, NULL, emaps_handler);
