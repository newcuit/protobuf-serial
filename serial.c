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
#include "serial.h"
#include "pbserial.h"

/**************************************************************************************
 * * FunctionName   : serial_write()
 * * Description    : 串口数据写入
 * * EntryParameter : fd,指向串口句柄， msg，指向待写入数据， len,数据长度
 * * ReturnValue    : 返回写入长度或者错误码
 * **************************************************************************************/
int serial_write(int fd, const char *msg, size_t n)
{
	size_t len;
	int written, ret;

	// 1.初始化写入数据信息
	written = 0;
	if (n == 0)
		len = strlen(msg);
	else
		len = n;
	// 2.直到写完所有数据
	do {
		ret = write(fd, msg + written, len - written);
		if (ret < 0 && (errno == EINTR || errno == EAGAIN)) {
			continue;
		} else if (ret < 0) {
			syslog(LOG_ERR, "write failed, error: %s", strerror(errno));
			return ret;
		}
		written += ret;
	} while (written < len);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : serial_read()
 * * Description    : 串口数据读取
 * * EntryParameter : fd,指向串口句柄， buffer，指向待读入数据缓冲， len,数据长度
 * * ReturnValue    : 返回读出的长度或者错误码
 * **************************************************************************************/
int serial_read(int fd, char *buffer, size_t n)
{
	int pos = 0;
	size_t count;

	// 1.直到读到串口没有数据或者buffer满了为止
	for (count = 0;n - pos > 0; pos += count) {
		do { 
			count = read(fd, buffer + pos, n - pos);
		} while (count < 0 && errno == EINTR);

		// 读取的数据长度小于或者等于0 表示没有数据， 跳出返回
		if(count <= 0) break;
	}

	return pos;
}

/**************************************************************************************
 * * FunctionName   : device_init初始化串口
 * * Description    : 打开串口
 * * EntryParameter : device,指向串口设备名字, baud,串口波特率
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
int device_init(const char *device, int baud)
{
	int fd;
	int flag;
	struct termios ios;

	// 1. 打开串口设备
	fd = open(device, O_RDWR | O_NOCTTY);
	if (unlikely(fd < 0)) {
		syslog(LOG_ERR, "open %s failed, error: %s", device, strerror(errno));
		return -1;
	}

	// 2.设置串口通信功能
	memset(&ios, 0x00, sizeof(ios));
	cfmakeraw(&ios);
	ios.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
	ios.c_cflag |= (CS8 | CLOCAL | CREAD);
	ios.c_lflag = 0;
	ios.c_oflag = 0;

	// 3.设置波特率
	switch(baud) {
	case 9600:
		cfsetispeed(&ios,B9600);
		cfsetospeed(&ios,B9600);
		break;
	case 19200:
		cfsetispeed(&ios,B19200);
		cfsetospeed(&ios,B19200);
		break;
	case 38400:
		cfsetispeed(&ios,B38400);
		cfsetospeed(&ios,B38400);
		break;
	case 115200:
		cfsetispeed(&ios,B115200);
		cfsetospeed(&ios,B115200);
		break;
	case 460800:
		cfsetispeed(&ios,B460800);
		cfsetospeed(&ios,B460800);
		break;
	case 500000:
		cfsetispeed(&ios,B500000);
		cfsetospeed(&ios,B500000);
		break;
	case 921600:
		cfsetispeed(&ios,B921600);
		cfsetospeed(&ios,B921600);
		break;
	defalut:
		syslog(LOG_ERR, "%s error baud %d", device, baud);
	}
	DEBUG("use baud %d\n", baud);

	ios.c_cc[VTIME] = 0;
	ios.c_cc[VMIN] = 0;

	// 4.参数写入设备
	tcsetattr( fd, TCSAFLUSH, &ios);

	fcntl(fd, F_GETFL, &flag);
	fcntl(fd, F_SETFL, (flag | O_NONBLOCK) & ~O_ASYNC);

	return fd;
}

/**************************************************************************************
 * * FunctionName   : device_deinit解初始化串口
 * * Description    : 关闭串口
 * * EntryParameter : fd,指向串口设备套接字
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
int device_deinit(int fd)
{
	struct termios ios;

	if(unlikely(fd <= 0)) return 0;

	//tcgetattr(fd, &ios);
	//tcsetattr(fd, TCSANOW, &ios);
	close(fd);
}
