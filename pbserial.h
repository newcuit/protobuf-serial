#ifndef _PBSERIAL_H__
#define _PBSERIAL_H__ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <syslog.h>

/**************************************************************************************
* Description    : 字节交换函数
**************************************************************************************/
#define BYTE_SWAP16(x)      __builtin_bswap16(x)
#define BYTE_SWAP32(x)      __builtin_bswap32(x)
#define BYTE_SWAP64(x)      __builtin_bswap64(x)

/**************************************************************************************
 * * Description    : 期望值优化
 * **************************************************************************************/
#define unlikely(Exp)       __builtin_expect(!!(Exp), 0)  // 期望位真
#define likely(exp)         __builtin_expect(!!(exp), 1)  // 期望为假

/**************************************************************************************
 * * Description    : 定义头部幻术
 * **************************************************************************************/
#define TRANS_MAGIC         0x55443322

/**************************************************************************************
 * * Description    : 定义协议处理回调
 * **************************************************************************************/
typedef int (*id_handler)(int);
typedef int (*handler_t)(int, char *,int);

/**************************************************************************************
 * * Description    : 协议链表定义
 * **************************************************************************************/
struct id_proto {
	uint8_t id;
	handler_t handler;
	id_handler init;
	id_handler deinit;
	struct id_proto *next;
};

/**************************************************************************************
 * * Description    : 传输数据头部定义
 * **************************************************************************************/
struct transport {
    uint32_t magic;                   // 传输头部标记，默认为固定值：0x55，0x44，0x33，0x22
    uint32_t length;                  // 后续所有字段的长度总和
    uint8_t id;                       // 标识数据类型
    uint8_t csum;                     // 数据段的校验码
    uint8_t data[0];                  // 通过protobuf生成的数据内容
}__packed;

/**************************************************************************************
* FunctionName   : pack_be8()
* Description    : 8位大端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_be8(const uint8_t src, uint8_t *dst)
{
    *dst = src;
    return sizeof(uint8_t);
}

/**************************************************************************************
* FunctionName   : pack_be8()
* Description    : 8位小端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_le8(const uint8_t src, uint8_t *dst)
{
    *dst = src;
    return sizeof(uint8_t);
}

/**************************************************************************************
* FunctionName   : pack_be16()
* Description    : 16位大端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_be16(const uint16_t src, uint16_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP16(src);
    #endif

    return sizeof(uint16_t);
}

/**************************************************************************************
* FunctionName   : pack_le16()
* Description    : 16位小端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_le16(const uint16_t src, uint16_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = BYTE_SWAP16(src);
    #else
    *dst =  src;
    #endif

    return sizeof(uint16_t);
}

/**************************************************************************************
* FunctionName   : pack_be32()
* Description    : 32位大端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_be32(const uint32_t src, uint32_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP32(src);
    #endif

    return sizeof(uint32_t);
}

/**************************************************************************************
* FunctionName   : pack_le32()
* Description    : 32位小端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_le32(const uint32_t src, uint32_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst =  BYTE_SWAP32(src);
    #else
    *dst = src;
    #endif

    return sizeof(uint32_t);
}

/**************************************************************************************
* FunctionName   : pack_be64()
* Description    : 64位大端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
// FIXME:对结构体中packed对其的64位数据打包会崩溃
static inline int8_t pack_be64(const uint64_t src, uint64_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP64(src);
    #endif

    return sizeof(uint64_t);
}

/**************************************************************************************
* FunctionName   : pack_le64()
* Description    : 64位小端格式打包
* EntryParameter : src,原始数据, *dst,目标数据指针
* Returnsrcue    : 返回打包后数据长度
**************************************************************************************/
static inline int8_t pack_le64(const uint64_t src, uint64_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = BYTE_SWAP64(src);
    #else
    *dst = src;
    #endif

    return sizeof(uint64_t);
}

/**************************************************************************************
* FunctionName   : unpack_be8()
* Description    : 8位大端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_be8(const uint8_t src, uint8_t *dst)
{
    *dst = src;
    return sizeof(uint8_t);
}

/**************************************************************************************
* FunctionName   : unpack_le8()
* Description    : 8位小端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_le8(const uint8_t src, uint8_t *dst)
{
    *dst = src;
    return sizeof(uint8_t);
}

/**************************************************************************************
* FunctionName   : unpack_be16()
* Description    : 16位大端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_be16(const uint16_t src, uint16_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP16(src);
    #endif

    return sizeof(uint16_t);
}

/**************************************************************************************
* FunctionName   : unpack_le16()
* Description    : 16位小端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_le16(const uint16_t src, uint16_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst =  BYTE_SWAP16(src);
    #else
    *dst = src;
    #endif

    return sizeof(uint16_t);
}

/**************************************************************************************
* FunctionName   : unpack_be32()
* Description    : 32位大端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_be32(const uint32_t src, uint32_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP32(src);
    #endif

    return sizeof(uint32_t);
}

/**************************************************************************************
* FunctionName   : unpack_le32()
* Description    : 32位小端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_le32(const uint32_t src, uint32_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = BYTE_SWAP32(src);
    #else
    *dst = src;
    #endif

    return sizeof(uint32_t);
}

/**************************************************************************************
* FunctionName   : unpack_be64()
* Description    : 64位大端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_be64(const uint64_t src, uint64_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = src;
    #else
    *dst = BYTE_SWAP16(src);
    #endif

    return sizeof(uint64_t);
}

/**************************************************************************************
* FunctionName   : unpack_le64()
* Description    : 64位小端格式解包
* EntryParameter : src,原始数据, *dst,目标数据指针
* ReturnValue    : 返回解包后数据长度
**************************************************************************************/
static inline int8_t unpack_le64(const uint64_t src, uint64_t *dst)
{
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    *dst = BYTE_SWAP16(src);
    #else
    *dst = src;
    #endif

    return sizeof(uint64_t);
}

/**************************************************************************************
 * * FunctionName   : memdup()
 * * Description    : 类似于strdup,内存拷贝
 * * EntryParameter : src, 拷贝的内存数据， len，拷贝的内存数据长度
 * * ReturnValue    : 返回复制后的新内存地址
 * **************************************************************************************/
#define memdup(src, len)  ({         \
	void *p = malloc(len);       \
	if(p != NULL) {              \
		memcpy(p, src, len);     \
	}                            \
	(void *)p;                   \
})

/**************************************************************************************
 * * FunctionName   : id_register()
 * * Description    : 注册通信数据ID
 * * EntryParameter : id,指向id相关数据信息
 * * ReturnValue    : 返回0
 * **************************************************************************************/
int id_register(struct id_proto *id);

/**************************************************************************************
 * * FunctionName   : trans_send()
 * * Description    : MCU发送数据到MPU
 * * EntryParameter : data，指向发送的数据， len,指向发送数据长度
 * * ReturnValue    : 返回发送状态或者长度
 * ************************************************************************************/
int packages_send(int fd, uint8_t id, char *data, int len);

/**************************************************************************************
* Description    : 定义协议注册函数
**************************************************************************************/
#define register_id(ID, INIT, DEINIT, HANDLER) \
static struct id_proto id_##ID##_##HANDLER= { \
	.id = ID, \
	.init = INIT, \
	.handler = HANDLER,  \
	.deinit = DEINIT,  \
}; \
static void  __attribute__((constructor)) __reg_proto_##ID(void) \
{ \
	id_register(&id_##ID##_##HANDLER); \
}

extern int _debug;
#define DEBUG(x...) if(_debug) printf(x);

#endif /* _PBSERIAL_H__ */
