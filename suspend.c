#include "id.h"
#include "pbserial.h"
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
 * * FunctionName   : handle_suspend_data()
 * * Description    : 设备进入休眠
 * * EntryParameter : fd,指向应答接口
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_suspend_data(int fd)
{
	return 0;
}

/**************************************************************************************
 * * FunctionName   : suspend_handler()
 * * Description    : suspend数据处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int suspend_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__SUSPEND: handle_suspend_data(fd);
	break;
	}
	subid__free_unpacked(msg, NULL);

	return 0;
}

// 注册ID
register_id(SUSPEND_ID, suspend_handler);
