#ifndef _SERIAL_H_
#define _SERIAL_H_

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include "pbserial.h"

/**************************************************************************************
 * * FunctionName   : serial_write()
 * * Description    : 串口数据写入
 * * EntryParameter : fd,指向串口句柄， msg，指向待写入数据， len,数据长度
 * * ReturnValue    : 返回长度
 * **************************************************************************************/
int serial_write(int fd, const char *msg, size_t n);

/**************************************************************************************
 * * FunctionName   : serial_read()
 * * Description    : 串口数据读取
 * * EntryParameter : fd,指向串口句柄， buffer，指向待读入数据缓冲， len,数据长度
 * * ReturnValue    : 返回长度
 * **************************************************************************************/
int serial_read(int fd, char *buffer, size_t n);

/**************************************************************************************
 * * FunctionName   : device_init初始化串口
 * * Description    : 打开串口
 * * EntryParameter : device,指向串口设备名字, baud,串口波特率
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
int device_init(const char *device, int baud);

/**************************************************************************************
 * * FunctionName   : device_deinit解初始化串口
 * * Description    : 关闭串口
 * * EntryParameter : fd,指向串口设备套接字
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
int device_deinit(int fd);
#endif
