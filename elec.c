#include "id.h"
#include "pbserial.h"
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
 * * FunctionName   : handle_elec_data()
 * * Description    : 处理GPS数据
 * * EntryParameter : data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_elec_data(ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Gps *elec = NULL;

	for (i=0; i<n_data; i++) {
		elec = gps__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(elec == NULL || elec->nmea.data == NULL)) continue;

		// Fix me, elec package
		gps__free_unpacked(elec, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : elec_handler()
 * * Description    : elec数据处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int elec_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__DATA: handle_elec_data(msg->subdata, msg->n_subdata);
	break;
	}
	subid__free_unpacked(msg, NULL);

	return 0;
}

// 注册ID
register_id(ELEC_ID, elec_handler);
