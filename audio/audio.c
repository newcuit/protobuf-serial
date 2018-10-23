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
#define MIN_TRANSFER_SIZE               8000

/**************************************************************************************
* Description    : 定义语音音频数据块结构
**************************************************************************************/
struct voice {
	int format;                                 // 当前块音频数据格式
	struct {
		int len;                                // 当前块音频数据长度
		char *data;                             // 当期快音频数据内容
	} wav;
	struct list_head list;                      // 链表头
};

/**************************************************************************************
* Description    : 定义audio数据块结构
**************************************************************************************/
static struct audio {
	int fd;                                    // 发送套接字
	int inited;                                // 表明当前结构是否初始化

	struct {
		int fd;                               // PCM句柄
		pthread_t tid;                        // 播放pcm线程
		pthread_cond_t cond;                  // 播放线程条件
		pthread_mutex_t mutex;                // 播放线程锁
		struct list_head voice_list;
	} voice;                                  // pcm发生结构
	struct {
		int format;                           // 录音上次格式
		int running;                          // 录音状态
		char *buffer;                         // 录音数据缓冲区
		char *voice_buffer;                   // 录音发送数据缓存区
		int voice_buf_len;                    // 录音发送缓冲区数据长度
		int voice_min_size;                   // 录音发送阈值, 超过则上报给mpu
		pthread_t tid;                        // 录音pcm线程
		pthread_cond_t cond;                  // 录音线程条件
		pthread_mutex_t mutex;                // 录音线程锁
	} record;                                 // pcm录音结构
} sound;

/**************************************************************************************
* Description    : 函数申明
**************************************************************************************/
static int send_audio_data(struct audio *ad, unsigned char *data, int len);

/**************************************************************************************
 * * FunctionName   : ql_cb_playback()
 * * Description    : 播放音频函数
 * * EntryParameter : fd, PCM句柄，data，指向音频数据, size,音频数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int ql_audio_playback(int fd, char *data, int size)
{
	int pos = 0;
	int length = 0;

	do {
		length = Ql_AudPlayer_Play(fd, &data[pos], size - pos);
		if(unlikely(length < 0)) break;

		pos += length;
	} while(pos < size);

	return size;
}

/**************************************************************************************
 * * FunctionName   : insert_audio_block()
 * * Description    : 插入一个音频数据处理块， 由线程使用
 * * EntryParameter : ad,,指向音频结构,format,数据格式,data,数据内容,len,数据内容长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static inline int insert_audio_block(struct audio *ad, int format, char *data, int len) 
{
	struct voice *inode = (struct voice *)malloc(sizeof(struct voice));
	
	if(unlikely(inode == NULL)) {
		DEBUG("%s can't malloc %d!!!\n", __func__, sizeof(struct voice));
		return -1;
	}

	inode->format = format;
	if(likely(data && len > 0)) {
		inode->wav.data = memdup(data, len);
		inode->wav.len = len;
	} else {
		inode->wav.data = NULL;
		inode->wav.len = 0;
	}

	pthread_mutex_lock(&ad->voice.mutex);
	list_add_tail(&inode->list, &ad->voice.voice_list);
	pthread_mutex_unlock(&ad->voice.mutex);
	pthread_cond_signal(&ad->voice.cond);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : audio_thread()
 * * Description    : audio数据处理线程
 * * EntryParameter : arg,指向音频结构
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static void *audio_thread(void *arg)
{
	struct voice *vsound = NULL;
	struct timespec timeout = {0,0}; 
	struct audio *ad = (struct audio *)arg;

	DEBUG("open audio player thread\n");
	while (ad->inited) {
		if(likely(list_empty(&ad->voice.voice_list))) {
			timeout.tv_sec = time(NULL) + 1;
			if(pthread_cond_timedwait(&ad->voice.cond, &ad->voice.mutex,
					&timeout) == ETIMEDOUT) {
				continue;
			}
		}
		// 1.获取音频结点
		vsound = list_first_entry(&ad->voice.voice_list, struct voice, list);
		if(unlikely(vsound == NULL)) {
			pthread_mutex_unlock(&ad->voice.mutex);
			continue;
		}
		
		list_del(&vsound->list);
		pthread_mutex_unlock(&ad->voice.mutex);
		//DEBUG("audio get voice format:%d\n",vsound->format);

		// 2.处理音频播放
		if(vsound->wav.data != NULL &&vsound->wav.len > 0) {
			ql_audio_playback(ad->voice.fd, vsound->wav.data, vsound->wav.len);
		}

		// 3.如果存在音频数据，释放对应的内存
		if(vsound->wav.data) free(vsound->wav.data);

		// 4.释放结点
		free(vsound);
	}
}

/**************************************************************************************
 * * FunctionName   : record_thread()
 * * Description    : 录音数据处理线程
 * * EntryParameter : arg,指向音频结构
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static void *record_thread(void *arg)
{
	int bufsize = 0;
	struct audio *ad = (struct audio *)arg;

	ad->record.voice_buf_len = 0;
	while (likely(ad->inited)) {
		// 1.加锁
		pthread_mutex_lock(&ad->record.mutex);
		if(!likely(ad->record.running)) {
			pthread_cond_wait(&ad->record.cond, &ad->record.mutex);
		}
		// 2. 获取录音
		if(!likely(ad->record.running)) {
			pthread_mutex_unlock(&ad->record.mutex);
			continue;
		}
		// 3. 获取录音
		bufsize = ql_voice_record_read(ad->record.buffer);

		// 4.将数据存放到发送缓冲区
		memcpy(ad->record.voice_buffer + ad->record.voice_buf_len, 
				ad->record.buffer, bufsize);
		ad->record.voice_buf_len += bufsize;

		// 5.满足条件，上报录音
		if(unlikely(ad->record.voice_buf_len >= ad->record.voice_min_size)) {
			send_audio_data(ad, ad->record.voice_buffer, ad->record.voice_buf_len);
			ad->record.voice_buf_len = 0;
		}

		// 6.解锁
		pthread_mutex_unlock(&ad->record.mutex);
	}
}

/*************************************************************************************** 
 * * FunctionName   : audio_can_send()
 * * Description    : 音频数据发送
 * * EntryParameter : ad,音频结构, data,指向录音数据,len，指向数据长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int send_audio_data(struct audio *ad, unsigned char *data, int len)
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
	message.format = ad->record.format;
	message.has_wav = 1;
	message.wav.data = data;
	message.wav.len = len;

	//DEBUG("AUDIO send format:%d, len:%d\n",message.format, len);

	// 3.打包子ID
	msg.subdata->len = audio__get_packed_size(&message);
	msg.subdata->data = malloc(msg.subdata->len);;

	// 4.打包获取的数据
	audio__pack(&message, msg.subdata->data);

	msglen = subid__get_packed_size(&msg);
	buffer = (char *)malloc(msglen);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	packages_send(ad->fd, AUDIO_ID, buffer, msglen);

	free(msg.subdata->data);
	free(buffer);

	return 0;
}

/*************************************************************************************** 
 * * FunctionName   : get_audio_data()
 * * Description    : 获取音频状态
 * * EntryParameter : ad,音频结构
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int get_audio_data(struct audio *ad)
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
	message.format = ad->record.format;
	message.has_wav = 0;

	//DEBUG("AUDIO get format:%d\n",message.format);

	// 3.打包子ID
	msg.subdata->len = audio__get_packed_size(&message);
	msg.subdata->data = (uint8_t *)oob;

	// 4.打包获取的数据
	audio__pack(&message, msg.subdata->data);
	subid__pack(&msg, (uint8_t *)buffer);

	// 5.发送数据
	return packages_send(ad->fd, AUDIO_ID, buffer, subid__get_packed_size(&msg));
}

/**************************************************************************************
 * * FunctionName   : handle_audio_data()
 * * Description    : 处理音频数据
 * * EntryParameter : ad,音频结构，data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int handle_audio_data(struct audio *ad, ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Audio *audio = NULL;

	// 1.音频数据发送到线程链表，由线程处理
	for (i = 0; i < n_data; i++) {
		audio = audio__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(audio == NULL || audio->has_wav != 1)) continue;

		insert_audio_block(ad, audio->format, audio->wav.data, audio->wav.len);
		//DEBUG("voice speek format:%d, length:%d\n",audio->format, audio->wav.len);

		audio__free_unpacked(audio, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : set_audio_data()
 * * Description    : 控制AUDIO
 * * EntryParameter : ad,音频结构,data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int set_audio_data(struct audio *ad, ProtobufCBinaryData *data, int n_data)
{
	int  i;
	void *ret;
	Audio *audio = NULL;

	audio = audio__unpack(NULL, data[0].len, data[0].data);
	if(unlikely(audio == NULL)) return -1;

	DEBUG("set format:%d, record current state:%s\n",audio->format,
			ad->record.running?"running":"idle");

	// 1. 开始录音(如果format为非None，则打开录音，并设置成对应格式)
	if(audio->format != FORMAT__NONE && !ad->record.running) {
		ad->record.format = audio->format;
		ad->record.running = !ad->record.running;
		ql_voice_record_dev_set(AUD_DOWN_LINK);
		ql_voice_record_open(QUEC_PCM_8K, QUEC_PCM_MONO);
		ad->record.buffer = malloc(ql_get_voice_record_buffer_len());
	}

	// 2. 结束录音(如果format为NONE，则关闭录音)
	if(ad->record.running && audio->format == FORMAT__NONE) {
		ad->record.running = !ad->record.running;
		pthread_mutex_lock(&ad->record.mutex);
		free(ad->record.buffer);
		ql_voice_record_close();
		ql_voice_record_dev_clear(AUD_DOWN_LINK);
		pthread_mutex_unlock(&ad->record.mutex);
	}

	DEBUG("wakeup record thread to %s\n",ad->record.running?"running":"idle");
	pthread_cond_signal(&ad->record.cond);
	audio__free_unpacked(audio, NULL);

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
	case IOC__GET: get_audio_data(&sound);
		break;
	case IOC__SET: set_audio_data(&sound, msg->subdata, msg->n_subdata);
		break;
	case IOC__DATA: handle_audio_data(&sound, msg->subdata, msg->n_subdata);
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
	void *ret = NULL;

	// 0.设置初始化状态
	sound.fd = fd;
	sound.inited = 1;

	// 1.初始化voice
	pthread_mutex_init(&sound.voice.mutex, NULL);
	pthread_cond_init(&sound.voice.cond, NULL);
	if(pthread_create(&sound.voice.tid, NULL, audio_thread, (void *)&sound) != 0) {
		DEBUG("audio create thread failed\n");
		goto destory_voice;
	}
	INIT_LIST_HEAD(&sound.voice.voice_list);
	sound.voice.fd = Ql_AudPlayer_Open(NULL, NULL);
	if(unlikely(sound.voice.fd < 0)) {
		goto destory_voice1;
	}

	// 2.初始化record
	sound.record.voice_buffer = (char *)malloc(MIN_TRANSFER_SIZE * 2);
	if(unlikely(sound.record.voice_buffer == NULL)) {
		goto destory_record;
	}
	sound.record.voice_min_size = MIN_TRANSFER_SIZE;
	if(pthread_create(&sound.record.tid, NULL, record_thread, (void *)&sound) != 0) {
		DEBUG("audio create thread failed\n");
		goto destory_record1;
	}
	sound.record.running = 0;
	sound.record.buffer = NULL;
	sound.record.format = FORMAT__NONE;
	pthread_mutex_init(&sound.record.mutex, NULL);
	pthread_cond_init(&sound.record.cond, NULL);
	DEBUG("audio thread running\n");

	return 0;
destory_record1:
	free(sound.record.voice_buffer);
destory_record:
	Ql_AudPlayer_Stop(sound.voice.fd);
	Ql_AudPlayer_Close(sound.voice.fd);
destory_voice1:
	pthread_cancel(sound.voice.tid);
	pthread_join(sound.voice.tid, &ret);
destory_voice:
	pthread_mutex_destroy(&sound.voice.mutex);
	pthread_cond_destroy(&sound.voice.cond);
	sound.inited = 0;

	return -1;
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

	if(unlikely(!sound.inited)) return 0;

	DEBUG("audio thread exit\n");
	// 1.结束voice线程
	Ql_AudPlayer_Stop(sound.voice.fd);
	Ql_AudPlayer_Close(sound.voice.fd);
	pthread_cancel(sound.voice.tid);
	pthread_join(sound.voice.tid, &ret);
	pthread_mutex_destroy(&sound.voice.mutex);
	pthread_cond_destroy(&sound.voice.cond);

	// 2.结束record线程
	pthread_cancel(sound.record.tid);
	pthread_join(sound.record.tid, &ret);
	pthread_mutex_destroy(&sound.record.mutex);
	pthread_cond_destroy(&sound.record.cond);
	free(sound.record.voice_buffer);

	if(likely(!sound.record.running)) {
		goto exit_done;
	}

	// 3.结束录音
	free(sound.record.buffer);
	ql_voice_record_close();
	ql_voice_record_dev_clear(AUD_DOWN_LINK);

exit_done:
	sound.inited = 0;

	return  0;
}

// 注册ID
register_id(AUDIO_ID, audio_init, audio_deinit, audio_handler);
