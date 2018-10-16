#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "id.h"
#include "pbserial.h"
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
* Description    : 定义最大protobuf数据和ID
**************************************************************************************/
#define MAX_ANT_SIZE     200
#define MAX_CONTEXT_SIZE 100

/**************************************************************************************
* Description    : 定义天线切换GPIO路径
**************************************************************************************/
static char *chg_path = "/sys/class/gpio/gpio75/value";

/**************************************************************************************
 * * FunctionName   : gpio_get()
 * * Description    : 获取GPIO状态
 * * EntryParameter : path,ADC状态路径
 * * ReturnValue    : 返回结果
 * **************************************************************************************/
static int gpio_get(char *path)
{
	int fd;
	int count;
	int value = 0;
	char buffer[40];

	if((fd = open(path, O_RDONLY)) < 0) {
		return -1;
	}
	memset(buffer, 0, 40);
	count = read(fd, buffer, 40);
	close(fd);

	if(count <= 0) return -1;

	return atoi(buffer);
}

/**************************************************************************************
 * * FunctionName   : gpio_set()
 * * Description    : 设置GPIO状态
 * * EntryParameter : path,ADC状态路径, value,GPIO值
 * * ReturnValue    : 返回结果
 * **************************************************************************************/
static int gpio_set(char *path, int value)
{
	int fd;
	int count;
	char buffer[40];

	if((fd = open(path, O_WRONLY)) < 0) {
		return -1;
	}
	count = snprintf(buffer, 40, "%d", value);
	write(fd, buffer, count);
	close(fd);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : read_antchg_data()
 * * Description    : 读取天线状态
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int read_antchg_data(int fd)
{
	AntChg ant = ANT_CHG__INIT;
	Subid msg  = SUBID__INIT;
	ProtobufCBinaryData pdata;
	char buffer[MAX_ANT_SIZE], oob[MAX_CONTEXT_SIZE];

	// 1.初始化protobuf结构
	msg.id = IOC__DATA;
	msg.n_subdata = 1;
	msg.subdata = &pdata;

	// 2.获取天线切换脚数据
	ant.chg = gpio_get(chg_path);
	msg.subdata->len = ant_chg__get_packed_size(&ant);
	msg.subdata->data = (uint8_t *)oob;

	// 3.打包protobuf数据
	ant_chg__pack(&ant, msg.subdata->data);
	subid__pack(&msg, (uint8_t *)buffer);

	// 4.发送数据到串口
	return packages_send(fd, ANT_CHG_ID, buffer, subid__get_packed_size(&msg));
}

/**************************************************************************************
 * * FunctionName   : write_antchg_data()
 * * Description    : 改变天线切换状态
 * * EntryParameter : data，指向protobuf数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int write_antchg_data(ProtobufCBinaryData *data, int n_data)
{
	int  i;
	AntChg *ant = NULL;

	for (i = 0; i < n_data; i++) {
		ant = ant_chg__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(ant == NULL)) continue;

		// 切换天线
		gpio_set(chg_path, !!ant->chg);
		ant_chg__free_unpacked(ant, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : antchg_handler()
 * * Description    : 天线切换处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int antchg_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__GET: read_antchg_data(fd);
	break;
	case IOC__SET: write_antchg_data(msg->subdata, msg->n_subdata);
	break;
	}
	subid__free_unpacked(msg, NULL);
}

// 注册ID
register_id(ANT_CHG_ID, antchg_handler);
