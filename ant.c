#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "id.h"
#include "pbserial.h"
#include "protobuf-c/data.pb-c.h"

/**************************************************************************************
* Description    : 定义最大protobuf数据和ID
**************************************************************************************/
#define MAX_ANT_SIZE     200
#define MAX_CONTEXT_SIZE 100

/**************************************************************************************
* Description    : 定义ADC路径
**************************************************************************************/
static char *adcm_path = "/sys/devices/qpnp-vadc-8/mpp4_vadc";
static char *adca_path = "/sys/devices/qpnp-vadc-8/mpp6_vadc";

/**************************************************************************************
 * * FunctionName   : vadc_get()
 * * Description    : 获取ADC数据
 * * EntryParameter : path,ADC状态路径
 * * ReturnValue    : 返回结果
 * **************************************************************************************/
static char *vadc_get(char *path)
{
	int fd;
	int count;
	int value = 0;
	char buffer[40];

	if((fd = open(path, O_RDONLY)) < 0) {
		return "unknown";
	}
	memset(buffer, 0, 40);
	count = read(fd, buffer, 40);
	close(fd);

	if(count <= 0) return "unknown";
	value = atoi(strchr(buffer, ':') + 1);

	// 电压大于2V为open， 0.9v到2v之间为天线在位，否则为短路
	if(value > 2000000) return "open";
	else if(value > 900000) return "ok";
	else return "short";

	return "unknown";
}

/**************************************************************************************
 * * FunctionName   : handle_ant_data()
 * * Description    : 读取天线检测数据
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_ant_data(int fd)
{
	Ant ant = ANT__INIT;
	Subid msg  = SUBID__INIT;
	ProtobufCBinaryData pdata;
	char buffer[MAX_ANT_SIZE], oob[MAX_CONTEXT_SIZE];

	// 1.初始化Protobuf数据
	msg.id = IOC__DATA;
	msg.n_subdata = 1;
	msg.subdata = &pdata;

	// 2.获取M 主天线数据
	ant.ant_m = vadc_get(adcm_path);
	// 3.获取A 负天线数据
	ant.ant_a = vadc_get(adca_path);
	msg.subdata->len = ant__get_packed_size(&ant);
	msg.subdata->data = (uint8_t *)oob;

	// 4.打包获取的数据
	ant__pack(&ant, msg.subdata->data);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	return packages_send(fd, ANT_ID, buffer, subid__get_packed_size(&msg));
}

/**************************************************************************************
 * * FunctionName   : ant_handler()
 * * Description    : 天线数据处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ant_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__GET: handle_ant_data(fd);
	break;
	}
	subid__free_unpacked(msg, NULL);
}

// 注册ID
register_id(ANT_ID, ant_handler);
