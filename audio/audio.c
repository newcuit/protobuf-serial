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
#include <speex/speex_echo.h>
#include "speex/speex_preprocess.h"
#include <protobuf-c/data.pb-c.h>

/**************************************************************************************
* Description    : 定义audio数据大小
**************************************************************************************/
#define MAX_AUDIO_SIZE                  200
#define MAX_CONTEXT_SIZE                100
#define MIN_TRANSFER_SIZE               8000
#define ECHO_TAIL                       2400         // cancel 300ms
#define EC20_SAMPLE_PER_20MS            (8000/50)    // 每20ms一帧数据（每20ms采样个数）
#define EC20_SAMPLE_BITS                2            // 采样位宽

/**************************************************************************************
* Description    : 定义语音音频数据块结构
**************************************************************************************/
struct speak_v {
	int len;                                // 当前块音频数据长度
	char *data;                             // 当期快音频数据内容
	struct list_head list;                  // 链表头
};

/**************************************************************************************
* Description    : 定义audio数据块结构
**************************************************************************************/
static struct audio {
	int fd;                                   // 发送套接字
	int pcm;                                  // PCM句柄
	int inited;                               // 程序初始化状态
	int record;                               // 录音状态 1,表示录音中， 0表示未录音
	int frame_size;                           // pcm一帧大小
	pthread_t tid;                            // 线程id
	pthread_cond_t wait;                      // 线程条件
	pthread_mutex_t lock;                     // 线程锁
	struct list_head list;                    // 音频列表
	SpeexEchoState *e_st;                     // 回音消除器
	SpeexPreprocessState *d_st;               // 噪音消除器
} sound;

static int sample_rate = 8000;                //采样率

/**************************************************************************************
* Description    : 函数申明
**************************************************************************************/
static int response_rec_data(struct audio *ad, unsigned char *data, int len);

/**************************************************************************************
 * * FunctionName   : wait_a_moment()
 * * Description    : 休息一会儿
 * * EntryParameter : ad, 全局音频模块
 * * ReturnValue    : 返回状态码
 * **************************************************************************************/
static int wait_a_moment(struct audio *ad)
{
	struct timespec timeout = {0,0};

	timeout.tv_sec = time(NULL) + 1;

	return pthread_cond_timedwait(&ad->wait, &ad->lock,&timeout);
}

/**************************************************************************************
 * * FunctionName   : insert_speak_v()
 * * Description    : 插入一个音频数据处理块， 由线程使用
 * * EntryParameter : ad,,指向音频结构,data,数据内容,len,数据内容长度
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static inline int insert_speak_v(struct audio *ad, char *data, int len) 
{
	struct speak_v *inode = (struct speak_v *)malloc(sizeof(struct speak_v));
	
	if(unlikely(inode == NULL)) {
		DEBUG("%s can't malloc %d!!!\n", __func__, sizeof(struct speak_v));
		return -1;
	}

	memset(inode, 0, sizeof(struct speak_v));
	if(likely(data && len > 0)) {
		inode->len = len;
		inode->data = memdup(data, len);
	}

	list_add_tail(&inode->list, &ad->list);
	pthread_cond_signal(&ad->wait);

	return 0;
}

/**************************************************************************************
 * * FunctionName   : alsa_sound_tasklet()
 * * Description    : 音频数据处理线程
 * * EntryParameter : private,指向音频结构
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static void *alsa_sound_tasklet(void *private)
{
	int ops = 0;
	int bufsize = 0;
	int play_ops = 0;
	char *play = NULL;
	struct speak_v *speak = NULL;
	struct audio *ad = (struct audio *)private;
	static char frame[MIN_TRANSFER_SIZE+1];
	static spx_int16_t in_frame[EC20_SAMPLE_PER_20MS];
	static spx_int16_t out_frame[EC20_SAMPLE_PER_20MS];

	while (1) {
		if(unlikely(!ad->inited)) {
			wait_a_moment(ad);
			continue;
		}

		// 1.加锁等待
		if(likely(!(ad->record != CODEC__NONE || !list_empty(&ad->list)))) {
			if(likely(wait_a_moment(ad) == ETIMEDOUT)) {
				continue;
			}
		}

		// 2. 获取音频数据
		if(!list_empty(&ad->list)) {
			speak = list_first_entry(&ad->list, struct speak_v, list);
			list_del(&speak->list);
		}
		pthread_mutex_unlock(&ad->lock);

		// 3.判断speak结构有效性， 不为NULL，表明有语音需要发送
		if(unlikely(speak!= NULL && speak->data != NULL)) {
			//speex_echo_state_reset(ad->e_st);
			play = speak->data;
			play_ops = 0;
		}

replay:
		pthread_mutex_lock(&ad->lock);
		// 4. 获取一帧录音,采用speex进行音频处理或者发送原始数据
		if(likely(ad->record == CODEC__SPEEX)) {
			bufsize = ql_voice_record_read((char *)in_frame);
			// 4.1 回声消除
			speex_echo_capture(ad->e_st, in_frame, out_frame);

			// 4.2 噪声抑制, 当vad为0的时候，表示当前声音是噪声或者静音
			//     当没有播放声音的时候，不进行噪音静音判断，只需要去噪声
			if(unlikely(speex_preprocess_run(ad->d_st, out_frame))) {
				// 4.3 将语音数据存放到发送缓冲区
				memcpy(frame + ops , out_frame, bufsize);
			} else if(unlikely(play != NULL && ops > 0)) {
				// 4.3 检测到无效数据（噪音或者静音）
				//     该数据(噪音或者静音)不发送，
				//     为了保持时间同步，将之前接收到的声音直接发送
				ops -= response_rec_data(ad, frame, ops);
				ops -= bufsize;
			} else {
				// 4.3 将没有播放情况下的，静音数据改为NULL数据存放到发送缓冲区
				memset(frame + ops , 0x00, bufsize);
			}
			ops += bufsize;
			//DEBUG("capture %d size\n", bufsize);
		} else if(unlikely(ad->record == CODEC__RAW)) {
			ops += ql_voice_record_read(frame + ops);
			//DEBUG("capture %d size\n", bufsize);
		}
		pthread_mutex_unlock(&ad->lock);

		// 5.发送一帧语音到codec
		if(unlikely(play != NULL)) {
			Ql_AudPlayer_Play(ad->pcm, play + play_ops, ad->frame_size);
			if(likely(ad->record == CODEC__SPEEX)) {
				speex_echo_playback(ad->e_st, (spx_int16_t *)(play + play_ops));
			}
			play_ops += ad->frame_size;
			//DEBUG("play:%d(%d) size\n", ad->frame_size, play_ops);
		}

		// 6.满足条件，上报录音
		if(unlikely(ops >= MIN_TRANSFER_SIZE)) {
			//DEBUG("transfer %d size\n", ops);
			ops -= response_rec_data(ad, frame, ops);
		}

		// 7.判断语音是否发送完成，还有语音未发送到codec， 到replay继续发送
		if(unlikely(speak != NULL && play_ops < speak->len)) {
			goto replay;
		}

		// 8.经过7后，如果speak不为NULL,则需要释放内存
		if(unlikely(speak != NULL)) {
			free(speak->data);
			free(speak);
		}
		play = NULL;
		speak = NULL;
	}
}

/*************************************************************************************** 
 * * FunctionName   : response_rec_data()
 * * Description    : 音频数据发送
 * * EntryParameter : ad,音频结构, data,指向录音数据,len，指向数据长度
 * * ReturnValue    : 返回发送数据长度
 * **************************************************************************************/
static int response_rec_data(struct audio *ad, unsigned char *data, int len)
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
	message.record = ad->record;
	message.has_data = 1;
	message.data.data = data;
	message.data.len = len;

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

	return len;
}

/**************************************************************************************
 * * FunctionName   : audio_play()
 * * Description    : 处理音频数据
 * * EntryParameter : ad,音频结构，data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int audio_play(struct audio *ad, ProtobufCBinaryData *data, int n_data)
{
	int  i;
	Audio *audio = NULL;

	// 1.音频数据发送到线程链表，由线程处理
	for (i = 0; i < n_data; i++) {
		audio = audio__unpack(NULL, data[i].len, data[i].data);
		if(unlikely(audio == NULL || audio->has_data != 1)) continue;

		insert_speak_v(ad, audio->data.data, audio->data.len);

		audio__free_unpacked(audio, NULL);
	}

	return 0;
}

/**************************************************************************************
 * * FunctionName   : set_record_state()
 * * Description    : 控制AUDIO
 * * EntryParameter : ad,音频结构,data，指向数据， n_data,数据个数
 * * ReturnValue    : 返回错误码
 * **************************************************************************************/
static int set_record_state(struct audio *ad, ProtobufCBinaryData *data, int n_data)
{
	int  i;
	void *ret;
	Audio *audio = NULL;

	audio = audio__unpack(NULL, data[0].len, data[0].data);
	if(unlikely(audio == NULL)) return -1;

	DEBUG("set record:%d, current state:%s\n",
			audio->record, ad->record != CODEC__NONE?"recording":"idle");

	// 1. 开始录音
	pthread_mutex_lock(&ad->lock);
	if(audio->record != CODEC__NONE && ad->record == CODEC__NONE) {
		ad->record = audio->record;
		ql_voice_record_dev_set(AUD_DOWN_LINK);
		ql_voice_record_open(QUEC_PCM_8K, QUEC_PCM_MONO);
	}

	// 2. 结束录音
	if(ad->record != CODEC__NONE && audio->record == CODEC__NONE) {
		ad->record = audio->record;
		ql_voice_record_close();
		ql_voice_record_dev_clear(AUD_DOWN_LINK);
	}
	pthread_mutex_unlock(&ad->lock);

	DEBUG("wakeup thread to %s\n",ad->record != CODEC__NONE?"recording":"idle");
	pthread_cond_signal(&ad->wait);
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
	case IOC__SET: set_record_state(&sound, msg->subdata, msg->n_subdata);
		break;
	case IOC__DATA: audio_play(&sound, msg->subdata, msg->n_subdata);
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
	int on;
	float f;
	int noise;
	void *ret = NULL;

	// 0.设置初始化状态
	sound.fd = fd;
	sound.frame_size = EC20_SAMPLE_PER_20MS * EC20_SAMPLE_BITS; // ec20 default is 320bytes a frame

	/* 1.噪音消除初始化， 每帧的大小（建议帧长为20ms）
	 * 帧长20ms等于160个采样, 采样率8000 
	 */
	sound.d_st = speex_preprocess_state_init(EC20_SAMPLE_PER_20MS, sample_rate);
	on = 1;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_DENOISE, &on);
	noise = -60; // 低于60db的都认为是噪声
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise); 
	on = 1;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_DEREVERB, &on);
	f = .0;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_DEREVERB_DECAY, &f);
	f = .0;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL, &f);

	// 2.设置噪音和静音检测模块
	on = 1;
	speex_preprocess_ctl(sound.d_st,SPEEX_PREPROCESS_SET_VAD, &on);
	on = 99;
	speex_preprocess_ctl(sound.d_st,SPEEX_PREPROCESS_SET_PROB_START, &on);
	on = 98;
	speex_preprocess_ctl(sound.d_st,SPEEX_PREPROCESS_SET_PROB_CONTINUE, &on);

	// 3.当近端活动状态时，设置残差回波衰减db
	on = -15;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,&on);
	// 4.设置噪声衰减分贝
	on = -40;
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,&on);

	// 5.创建回音抑制器
	sound.e_st = speex_echo_state_init(EC20_SAMPLE_PER_20MS, ECHO_TAIL);
	speex_echo_ctl(sound.e_st, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate);
	speex_preprocess_ctl(sound.d_st, SPEEX_PREPROCESS_SET_ECHO_STATE, sound.e_st);

	// 6.初始化锁
	pthread_mutex_init(&sound.lock, NULL);
	pthread_cond_init(&sound.wait, NULL);

	// 7.初始化voice相关
	INIT_LIST_HEAD(&sound.list);
	Ql_AudPlayer_SetBufsize_ms(NULL/*"hw:0,0"*/,20);/* store and playing 20ms */
	Ql_clt_set_mixer_value("SEC_AUX_PCM_RX Audio Mixer MultiMedia1", 1, "1");
	Ql_clt_set_mixer_value("MultiMedia1 Mixer SEC_AUX_PCM_UL_TX", 1, "1");

	sound.pcm = Ql_AudPlayer_Open(NULL, NULL);
	if(unlikely(sound.pcm < 0)) {
		goto destory_init1;
	}

	// 8.初始化record
	sound.record = CODEC__NONE;

	// 9.初始化线程
	if(pthread_create(&sound.tid, NULL, alsa_sound_tasklet,(void *)&sound) != 0) {
		DEBUG("audio create thread failed\n");
		goto destory_init2;
	}

	sound.inited = 1;
	DEBUG("audio thread running\n");

	return 0;
destory_init2:
	Ql_AudPlayer_Stop(sound.pcm);
	Ql_AudPlayer_Close(sound.pcm);
destory_init1:
	pthread_mutex_destroy(&sound.lock);
	pthread_cond_destroy(&sound.wait);

	speex_echo_state_destroy(sound.e_st);
	speex_preprocess_state_destroy(sound.d_st);
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

	DEBUG("audio thread exit\n");

	if(!sound.inited) return 0;

	// 1.结束voice
	Ql_AudPlayer_Stop(sound.pcm);
	Ql_AudPlayer_Close(sound.pcm);

	// 2.结束线程
	pthread_cancel(sound.tid);
	pthread_join(sound.tid, &ret);
	pthread_mutex_destroy(&sound.lock);
	pthread_cond_destroy(&sound.wait);

	// 3.销毁speex相关
	speex_echo_state_destroy(sound.e_st);
	speex_preprocess_state_destroy(sound.d_st);

	// 4.结束录音
	if(likely(sound.record)) {
		ql_voice_record_close();
		ql_voice_record_dev_clear(AUD_DOWN_LINK);
	}
	sound.inited = 0;

	return  0;
}

// 注册ID
register_id(AUDIO_ID, audio_init, audio_deinit, audio_handler);
