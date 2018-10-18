#include "id.h"
#include "gps.h"
#include "pbserial.h"
#include "iav2hp.h"
#include "minmea.h"
#include "ql_oe.h"
#include "list.h"
#include <time.h>
#include <memory.h>
#include <pthread.h>
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
* Description    : 定义audio数据大小
**************************************************************************************/
#define MAX_AUDIO_SIZE                  200
#define MAX_CONTEXT_SIZE                100

/**************************************************************************************
* Description    : 定义audio数据块结构
**************************************************************************************/
struct audio {
	int type;                                   // 当前块类型，如 录音、音频数据
	int format;                                 // 当前块音频数据格式
	struct {
		int len;                                // 当前块音频数据长度
		char *data;                             // 当期快音频数据内容
	} wav;
	struct list_head list;                      // 链表头
};

/**************************************************************************************
* Description    : 定义线程ID,条件锁，互斥锁和音频格式
**************************************************************************************/
static pthread_t audio_tid;
enum { RECORD_TYPE, VOICE_TYPE};
static struct list_head audio_list;
static int current_format = FORMAT__NONE;
static pthread_cond_t audio_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int send_audio_data(int fd, unsigned char *data, int len);

static int record_fd = -1; // 录音使用应答套接字, 不录音的时候是-1，否则大于等于0
static int playbacking = 0; // 播放音频标志， 1表示正在播放声音， 0表示没播放

/**************************************************************************************
 * * FunctionName   : ql_cb_playback()
 * * Description    : 播放音频回调函数
 * * EntryParameter : hdl句柄， result,状态
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ql_cb_playback(int hdl, int result)
{
	DEBUG("hdl=%d, result=%d\n", hdl, result);
	if(result == AUD_PLAYER_FINISHED || result == AUD_PLAYER_NODATA) {
		playbacking = 0;
	}
	return 0;
}

/**************************************************************************************
 * * FunctionName   : ql_cb_playback()
 * * Description    : 播放音频函数
 * * EntryParameter : data，指向音频数据, size,音频数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ql_audio_playback(char *data, int size)
{
	int devfd;
	int pos = 0;
	int length = 0;

	// 1.打开音频控制器
	devfd = Ql_AudPlayer_Open(NULL, ql_cb_playback);
	if(devfd < 0) {
		DEBUG("open player failed\n");
		return -1;
	}
	pos = 0;
	playbacking = 1;
	DEBUG("Play audio(%d)\n", size);
	// 2.填充播放声音
	do {
		length = Ql_AudPlayer_Play(devfd, &data[pos], size - pos);
		if(unlikely(length < 0)) break;

		pos += length;
	} while(pos < size);

	// 3.等待录音播放完成
	while(playbacking == 1) sleep(1);
	DEBUG("Play audio(%d) finished\n", size);

	// 4.关闭音频播放
	Ql_AudPlayer_Stop(devfd);
	Ql_AudPlayer_Close(devfd);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : ql_cb_record()
 * * Description    : 录音回调
 * * EntryParameter : result,状态码， buf,录到的数据， len，录到的数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ql_cb_record(int result, unsigned char *buf, unsigned int len)
{
	switch(result) {
	case AUD_RECORDER_ERROR:
		DEBUG("record error\n");
		break;
	case AUD_RECORDER_START:
		DEBUG("record record len:%d\n", len);
		send_audio_data(record_fd, buf, len);
		break;
	case AUD_RECORDER_PAUSE:
		DEBUG("record pause\n");
		break;
	case AUD_RECORDER_RESUME:
		DEBUG("record resume\n");
		break;
	case AUD_RECORDER_FINISHED:
		DEBUG("record finished\n");
		break;
	}
	return 0;
}

/**************************************************************************************
 * * FunctionName   : ql_audio_record()
 * * Description    : 录音
 * * EntryParameter : fd, 发送套接字, format,录音传输格式
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ql_audio_record(int fd, int format)
{
	if(unlikely(record_fd >= 0)) {
		DEBUG("record are already running\n");
		return -1;
	}
	//开始录音(如果format为非None，则打开录音，并设置成对应格式)
	if(current_format == FORMAT__NONE && format != FORMAT__NONE) {
		Ql_clt_set_mixer_value("SEC_AUX_PCM_RX Audio Mixer MultiMedia1",1,"1");
		Ql_clt_set_mixer_value("MultiMedia1 Mixer SEC_AUX_PCM_UL_TX",1,"1");
		record_fd = fd;
		if(unlikely(Ql_AudRecorder_Open(NULL,ql_cb_record) != 0)) {
			DEBUG("open record device failed\n");
		}
		if(unlikely(Ql_AudRecorder_StartRecord() != 0)) {
			DEBUG("start record failed\n");
		}
		DEBUG("record running\n");
	}
	//结束录音(如果format为NONE，则关闭录音)
	if(current_format != FORMAT__NONE && format == FORMAT__NONE) {
		Ql_AudRecorder_Stop();
		Ql_AudRecorder_Close();
		record_fd = -1;
		DEBUG("record close\n");
	}
	//Fixme, 中间录音格式变更
	// TODO
	current_format = format;
	return 0;
}

/**************************************************************************************
 * * FunctionName   : insert_audio_block()
 * * Description    : 插入一个音频数据处理块， 由线程使用
 * * EntryParameter : type,数据块类型，format,数据格式，data，数据内容，len，数据内容长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static inline int insert_audio_block(int type, int format, char *data, int len) 
{
	struct audio *inode = (struct audio *)malloc(sizeof(struct audio));
	
	if(unlikely(inode == NULL)) {
		DEBUG("%s can't malloc %d!!!\n", __func__, sizeof(struct audio));
		return -1;
	}

	inode->type = type;
	inode->format = format;
	if(data && len > 0) {
		inode->wav.data = strndup(data, len);
		inode->wav.len = len;
	} else {
		inode->wav.data = NULL;
		inode->wav.len = 0;
	}

	pthread_mutex_lock(&audio_mutex);
	list_add_tail(&inode->list, &audio_list);
	pthread_mutex_unlock(&audio_mutex);
	pthread_cond_signal(&audio_cond);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : audio_thread()
 * * Description    : audio数据处理线程
 * * EntryParameter : arg,指向应答套接字
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static void *audio_thread(void *arg)
{
	int fd = (int)arg;
	struct audio *inode = NULL;
	struct timespec timeout = {0,0}; 

	while (1) {
		if(list_empty(&audio_list)) {
			timeout.tv_sec = time(NULL) + 1;
			if(pthread_cond_timedwait(&audio_cond, &audio_mutex,
					&timeout) == ETIMEDOUT) {
				continue;
			}
		}
		// 1.获取结点
		inode = list_first_entry(&audio_list, struct audio, list);
		if(unlikely(inode == NULL)) {
			pthread_mutex_unlock(&audio_mutex);
			continue;
		}
		
		list_del(&inode->list);
		pthread_mutex_unlock(&audio_mutex);
		DEBUG("audio get type:%s format:%d\n",inode->type == RECORD_TYPE?
				"record":"voice", inode->format);

		// 2.处理录音函数
		if(inode->type == RECORD_TYPE) {
			ql_audio_record(fd, inode->format);
		}

		// 3.处理音频播放
		if(inode->type == VOICE_TYPE && inode->wav.data != NULL) {
			ql_audio_playback(inode->wav.data, inode->wav.len);
		}

		// 4.如果存在音频数据，释放对应的内存
		if(inode->wav.data) free(inode->wav.data);

		// 5.释放结点
		free(inode);
	}
}

/*************************************************************************************** 
 * * FunctionName   : audio_can_send()
 * * Description    : 音频数据发送
 * * EntryParameter : fd,应答套接字, data,指向录音数据,len，指向数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int send_audio_data(int fd, unsigned char *data, int len)
{
	int msglen = 0;
	char *buffer = NULL;
	Subid msg  = SUBID__INIT;
	Audio message = AUDIO__INIT;
	ProtobufCBinaryData pdata;

	// 1.初始化Protobuf数据
	msg.id = IOC__DATA;
	msg.n_subdata = 1;
	msg.subdata = &pdata;

	// 2.打包音频数据
	message.format = current_format;
	message.has_wav = 1;
	message.wav.data = data;
	message.wav.len = len;

	DEBUG("AUDIO send format:%d, len:%d\n",message.format, len);

	// 3.打包子ID
	msg.subdata->len = audio__get_packed_size(&message);
	msg.subdata->data = malloc(msg.subdata->len);;

	// 4.打包获取的数据
	audio__pack(&message, msg.subdata->data);

	msglen = subid__get_packed_size(&msg);
	buffer = (char *)malloc(msglen);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	packages_send(fd, AUDIO_ID, buffer, msglen);

	free(msg.subdata->data);
	free(buffer);

	return 0;
}

/*************************************************************************************** 
 * * FunctionName   : get_audio_data()
 * * Description    : 获取音频状态
 * * EntryParameter : fd,指向应答的套接字
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int get_audio_data(int fd)
{
	Subid msg  = SUBID__INIT;
	Audio message = AUDIO__INIT;
	ProtobufCBinaryData pdata;
	char buffer[MAX_AUDIO_SIZE], oob[MAX_CONTEXT_SIZE];

	// 1.初始化Protobuf数据
	msg.id = IOC__DATA;
	msg.n_subdata = 1;
	msg.subdata = &pdata;

	// 2.打包音频格式数据
	message.format = current_format;
	message.has_wav = 0;

	DEBUG("AUDIO get format:%d\n",message.format);

	// 3.打包子ID
	msg.subdata->len = audio__get_packed_size(&message);
	msg.subdata->data = (uint8_t *)oob;

	// 4.打包获取的数据
	audio__pack(&message, msg.subdata->data);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	return packages_send(fd, AUDIO_ID, buffer, subid__get_packed_size(&msg));
}

/**************************************************************************************
 * * FunctionName   : handle_audio_data()
 * * Description    : 处理音频数据
 * * EntryParameter : data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_audio_data(ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Audio *audio = NULL;

	// 1.音频数据发送到线程链表，由线程处理
	for (i = 0; i < n_data; i++) {
		audio = audio__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(audio == NULL || audio->has_wav != 1)) continue;

		insert_audio_block(VOICE_TYPE, audio->format,audio->wav.data,audio->wav.len);
		DEBUG("voice speek format:%d, length:%d\n",audio->format, audio->wav.len);

		audio__free_unpacked(audio, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : set_audio_data()
 * * Description    : 控制AUDIO
 * * EntryParameter : data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int set_audio_data(ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Audio *audio = NULL;

	for (i = 0; i < n_data; i++) {
		audio = audio__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(audio == NULL)) continue;

		insert_audio_block(RECORD_TYPE, audio->format, NULL, 0);
		DEBUG("AUDIO ctl format:%d\n",audio->format);

		audio__free_unpacked(audio, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : audio_handler()
 * * Description    : audio数据处理函数
 * * EntryParameter : fd, 串口句柄， data，指向数据， len,数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int audio_handler(int fd, char *data, int len)
{
	Subid *msg = NULL;

	msg = subid__unpack(NULL, len, (uint8_t *)data);
	if(unlikely(msg == NULL)) return -1;

	switch (msg->id) {
	case IOC__DATA: handle_audio_data(msg->subdata, msg->n_subdata);
		break;
	case IOC__SET: set_audio_data(msg->subdata, msg->n_subdata);
		break;
	case IOC__GET: get_audio_data(fd);
		break;
	}
	subid__free_unpacked(msg, NULL);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : audio_init()
 * * Description    : audio初始化
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int audio_init(int fd)
{
	if(pthread_create(&audio_tid, NULL, audio_thread, (void *)fd) != 0) {
		DEBUG("audio create thread failed\n");
	} else {
		DEBUG("audio thread running\n");
	}
	INIT_LIST_HEAD(&audio_list);
	return 0;
}

/**************************************************************************************
 * * FunctionName   : audio_deinit()
 * * Description    : audio解初始化
 * * EntryParameter : fd, 串口句柄
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int audio_deinit(int fd)
{
	void *ret;

	// 1.等待音频播放结束
	while(playbacking == 1) {
		DEBUG("audio wait audio play end\n");
		sleep(1);
	}

	// 2.线程退出发现还在录音， 则停止录音
	if(unlikely(record_fd >= 0)) {
		DEBUG("audio close record\n");
		Ql_AudRecorder_Stop();
		Ql_AudRecorder_Close();
	}

	// 3.结束线程
	pthread_cancel(audio_tid);
	if(pthread_join(audio_tid, &ret) == 0) {
		DEBUG("audio thread exit ok\n");
	} else {
		DEBUG("audio thread exit failed\n");
	}
	return  0;
}

// 注册ID
register_id(AUDIO_ID, audio_init, audio_deinit, audio_handler);
