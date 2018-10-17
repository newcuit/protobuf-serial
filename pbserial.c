#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <syslog.h>
#include <execinfo.h>
#include "serial.h"
#include "pbserial.h"

#define LOG_TAG                         "pbserial" // 日志名字
#define BUFFER_FIFO_SIZE                2048    // 缓存暂时没有收全的protobuf数据包
#define BACKTRACE_SIZE                  100

int _debug = 0;                           // 调试开关
static int m_fd = -1;                           // 串口套接字
static struct id_proto *id_list = NULL;         // 指向所有id列表

/**************************************************************************************
 * * FunctionName   : chksum_xor()
 * * Description    : 计算校验
 * * EntryParameter : data，指向待校验数据， len,送数据长度
 * * ReturnValue    : 返回校验码
 * **************************************************************************************/
static uint8_t chksum_xor(uint8_t *data, int32_t len)
{
	int32_t i = 1;
	uint8_t csum = data[0];

	for (i = 1; i < len; i++) {
		csum ^= data[i];
	}
	return csum;
}

/**************************************************************************************
 * * FunctionName   : id_register()
 * * Description    : 注册通信数据ID
 * * EntryParameter : id,指向id相关数据信息
 * * ReturnValue    : 返回0
 * **************************************************************************************/
int id_register(struct id_proto *id)
{
	struct id_proto *i = id_list;

	// 1. 将id插入到列表尾部
	if (id_list == NULL) id_list = id;
	else {
		while (i->next != NULL) i = i->next;
		i->next = id;
	}
	DEBUG("%d registered\n", id)

	return 0;
}

/**************************************************************************************
 * * FunctionName   : iddata_send()
 * * Description    : 将通信数据传送给对应ID处理函数
 * * EntryParameter : fd, 串口句柄， id,指向id,data,id将要处理的数据，len，数据长度
 * * ReturnValue    : 返回0
 * **************************************************************************************/
static int iddata_send(int fd, uint8_t id, char *data, int len)
{
	struct id_proto *proto = id_list;

	while (unlikely(proto != NULL)) {
		if(likely(proto->id != id)) {
			proto = proto->next;
			continue;
		}

		// 对应ID处理数据
		return proto->handler(fd, data, len);
	}
	DEBUG("%d not registered\n", id)
	return -1;
}

/**************************************************************************************
 * * FunctionName   : do_packages()
 * * Description    : modem接收数据处理
 * * EntryParameter : data，指向MPU发送的数据， len,指向MPU发送数据长度
 * * ReturnValue    : 返回None
 * **************************************************************************************/
static int do_packages(int fd, char *data, int len)
{
	uint8_t id = 0;
    int32_t pos = 0;
	uint32_t magic = 0, length = 0;
	struct transport *tdata = NULL;
	static uint16_t data_left = 0;
	static char data_space[BUFFER_FIFO_SIZE];

	if(unlikely(data == NULL)) return -EINVAL;

	// 0.如果上次有数据未处理完， 这里添加到头部重新处理
	if(data_left + len > BUFFER_FIFO_SIZE) data_left = 0;

	memcpy(data_space + data_left, data, len);
	data_left = data_left + len;

	// 1.遍历整个数据区，寻找数据头部
	while ((data_left - pos) > (int32_t)sizeof(struct transport)) {
		tdata = (struct transport *)((uint8_t *)data_space + pos);

		// 头部数据，解析数据长度和ID
		unpack_be8(tdata->id, &id);
		unpack_be32(tdata->magic, &magic);
		unpack_be32(tdata->length, &length);

		// 1.1 判断是否为头部
		if(likely(magic != TRANS_MAGIC)) {
			pos++;continue;
		}

		// 检测数据长度有效性, 长度异常, 跳出循环
		if(unlikely(length > (data_left - pos - sizeof(struct transport)))) {
			break;
		}

		// 数据校验可靠性检测， 错误就重新尝试
		if(tdata->csum == chksum_xor((uint8_t *)tdata->data, length)) {
			DEBUG("recv %d length:%d\n", id, length)
			iddata_send(fd, id, (char *)tdata->data, length);
		} else {
			syslog(LOG_ERR,"recv data(%d) chksum fail !!!\n", id, length);
		}

		// 数据去掉头部数据
		pos += sizeof(struct transport);
	}

	data_left = data_left - pos;
	memcpy(data_space,  data_space + pos, data_left);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : trans_send()
 * * Description    : MCU发送数据到MPU
 * * EntryParameter : fd, 串口句柄， data，指向发送的数据， len,指向发送数据长度
 * * ReturnValue    : 返回发送状态或者长度
 * ************************************************************************************/
int packages_send(int fd, uint8_t id, char *data, int len)
{
	int32_t tdata_len = 0;
	struct transport *tdata = NULL;

	// 1.打包protobuf数据头部
	tdata_len = sizeof(struct transport) + len;
	tdata = (struct transport *)malloc(tdata_len);
	if(unlikely(tdata == NULL)) return -ENOMEM;

	// 2.初始化头部数据结构
	pack_be8(id, &tdata->id);
	pack_be32(len, &tdata->length);
	pack_be32(TRANS_MAGIC, &tdata->magic);

	memcpy(tdata->data, data, len);
	tdata->csum = chksum_xor((uint8_t *)tdata->data, len);
	// 3.发送数据到串口
	serial_write(fd, (char *)tdata, tdata_len);
	DEBUG("%d data send length:%d\n", id, len)

	free(tdata);
	return len;
}

/**************************************************************************************
 * * FunctionName   : uninstall_protoid()
 * * Description    : 解初始化ID
 * * EntryParameter : fd,指向应答套接字（串口）
 * * ReturnValue    : None
 * ************************************************************************************/
static void uninstall_protoid(int fd)
{
	struct id_proto *proto = id_list;

	while (unlikely(proto != NULL)) {
		DEBUG("id(%d) do exit\n", proto->id)
		if(proto->deinit) proto->deinit(fd);
		proto = proto->next;
	}
}

/**************************************************************************************
 * * FunctionName   : handle_INT()
 * * Description    : 信号处理函数
 * * EntryParameter : signum，信号值
 * * ReturnValue    : None
 * ************************************************************************************/
static void handle_INT(int signum)
{
	syslog(LOG_NOTICE, "Terminated by signal %d", signum);
	uninstall_protoid(m_fd);
	device_deinit(m_fd);
	closelog();
	exit(1);
}

/**************************************************************************************
 * * FunctionName   : pb_backtrace()
 * * Description    : 堆栈回溯
 * * EntryParameter : None
 * * ReturnValue    : None
 * ************************************************************************************/
static void pb_backtrace(int signum)
{
	int j, nptrs;
	char **strings;
	void *buffer[BACKTRACE_SIZE];

	printf("Ooops(%d): \n",signum);
	syslog(LOG_ERR,"Ooops(%d): \n", signum);
	nptrs = backtrace(buffer, BACKTRACE_SIZE);
	strings = backtrace_symbols(buffer, nptrs);
	if (strings != NULL) {
		for (j = 0; j < nptrs; j++) {
			printf("  [%02d] %s\n", j, strings[j]);
			syslog(LOG_ERR,"  [%02d] %s\n", j, strings[j]);
		}
		free(strings);
	}
	exit(1);
}

/**************************************************************************************
 * * FunctionName   : setup_signals()
 * * Description    : 初始化信号
 * * EntryParameter : None
 * * ReturnValue    : None
 * ************************************************************************************/
static void setup_signals()
{
	struct sigaction sa;
															    
	sa.sa_flags = 0;
	sa.sa_handler = handle_INT;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	//sa.sa_flags = 0;
	//sa.sa_handler = pb_backtrace;
	//sigaction(SIGBUS, &sa, NULL);
	//sigaction(SIGABRT, &sa, NULL);
	//sigaction(SIGSEGV, &sa, NULL);
	//sigaction(SIGFPE, &sa, NULL);
}

/**************************************************************************************
 * * FunctionName   : setup_protoid()
 * * Description    : 初始化ID
 * * EntryParameter : fd,指向应答套接字（串口）
 * * ReturnValue    : None
 * ************************************************************************************/
static void setup_protoid(int fd)
{
	struct id_proto *proto = id_list;

	while (unlikely(proto != NULL)) {
		DEBUG("id(%d) do init\n", proto->id)
		if(proto->init) proto->init(fd);
		proto = proto->next;
	}
}

/**************************************************************************************
 * * FunctionName   : usage()
 * * Description    : 帮助文档
 * * EntryParameter : app,应用名字
 * * ReturnValue    : None
 * ************************************************************************************/
static void usage(const char *app)
{
	fprintf(stderr, "Usage: %s -d device [-b 115200/9600] [-v] [-D]\n", app);
	exit(0);
}

/**************************************************************************************
 * * FunctionName   : main()
 * * Description    : 主函数入口
 * * EntryParameter : argc,参数个数， argv,指向参数指针
 * * ReturnValue    : 错误码
 * ************************************************************************************/
int main(int argc, char *argv[])
{
	int opt;
	int fd = 0;
	size_t len = 0;
	fd_set readfds;
	char buffer[1024];
	int baud = 115200;
	int daemonize = 0;
	char *device = NULL;

	// 1.解析命令行参数
	while (-1 != (opt = getopt(argc, argv, "d:b:vD"))) {
		switch (opt) {
			case 'd':
				device = optarg;
				break;
			case 'v':
				_debug = 1;
				break;
			case 'D':
				daemonize = 1;
				break;
			case 'b':
				baud = atoi(optarg);
				break;
			default:
				usage(argv[0]);
				break;
		}
	}

	// 2.设备未NULL，打印帮助信息
	if (device == NULL) {
		usage(argv[0]);
	}
	
	// 3.是否成为守护程序
	if (daemonize) {
		int i;
		daemon(0, 0);
		for (i = 0; i < getdtablesize(); i++) {
			if (close(i) < 0)
				break;
		}
	}

	// 4.打开日志
	openlog(LOG_TAG, LOG_CONS, LOG_DAEMON);

	// 5.安装信号接收函数
	setup_signals();

	// 6.初始化串口
	fd = device_init(device, baud);
	if (fd < 0) {
		DEBUG("device(%s) init failed\n", device)
		return -1;
	}

	// 7.初始化各ID
	setup_protoid(fd);

	// 8.任务处理
	m_fd = fd;
	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);

		// 检测是否有数据读
		if(unlikely(select(fd+1, &readfds, NULL, NULL, NULL) < 0)) {
			break;
		}
		// 读取数据
		memset(buffer, 0, 1024);
		len = serial_read(fd, buffer, 1024);
		if(unlikely(len <= 0)) continue;
		// 处理数据
		do_packages(fd, buffer, len);
	}
	// 9.关闭
	close(fd);
	closelog();

	return 0;
}
