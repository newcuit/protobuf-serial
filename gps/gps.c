#include "gps.h"
#include "minmea.h"

/**************************************************************************************
 * * Description    : 模块配置定义
 * **************************************************************************************/
#define GPS_NMEA_MAXLEN        82                      // NMEA语句最大长度

/**************************************************************************************
* MacroName      : GPS_MOVE_STEPS()
* Description    : 分离报文时移动步数
* EntryParameter : data,被移动数据, len,数据长度, step,移动步数
* ReturnValue    : None
**************************************************************************************/
#define GPS_MOVE_STEPS(data,len,step)  \
do{                                     \
	data += step;                       \
	len  -= step;                       \
}while(0)

/**************************************************************************************
* MacroName      : GPS_FILL_EOF()
* Description    : 分离报文时填结束符
* EntryParameter : data,被移动数据, len,数据长度
* ReturnValue    : None
**************************************************************************************/
#define GPS_FILL_EOF(data,len)         \
do{                                     \
	data[len] = '\0';                   \
}while(0)

/**************************************************************************************
* MacroName      : GPS_DELAY()
* Description    : 延时
* EntryParameter : ms,延时毫秒数
* ReturnValue    : None
**************************************************************************************/
#define GPS_DELAY(ms)      time_delayms(ms)

/**************************************************************************************
* FunctionName   : gps_begin()
* Description    : 获取NMEA报文的起始偏移
* EntryParameter : data,报文指针, len,报文长度
* ReturnValue    : 返回报文起始偏移
**************************************************************************************/
static inline uint16_t gps_begin(uint8_t *data, uint16_t len)
{
	uint16_t offset = 0;

	while(*data != '$' && len > offset){
		data++;
		offset++;
	}

	return offset;
}

/**************************************************************************************
* FunctionName   : gps_end()
* Description    : 获取NMEA报文的尾部偏移
* EntryParameter : data,报文指针, len,报文长度
* ReturnValue    : 返回报文尾部偏移
**************************************************************************************/
static inline uint16_t gps_end(uint8_t *data, uint16_t len)
{
	uint16_t offset = 0;
	
	while(data[0] != '\r' && data[1] != '\n' && len > offset){
		data++;
		offset++;
	}
	
	return offset;
}

/**************************************************************************************
* FunctionName   : gps_parse()
* Description    : 解析NMEA报文数据
* EntryParameter : sentence, NMEA报文指针
* ReturnValue    : None
**************************************************************************************/
static inline void gps_parse(struct gpsinfo *info, void *sentence)
{
	enum minmea_sentence_id nmea_id = 0;
	
	// 1.获取NEMA报文ID
	nmea_id = minmea_sentence_id(sentence, false);
	
	// 2.解析NEMA报文
	switch(nmea_id){
	case MINMEA_SENTENCE_RMC:
		if(true == minmea_parse_rmc(&info->rmc, sentence)){
		}
		break;
	case MINMEA_SENTENCE_GGA:
		if(true == minmea_parse_gga(&info->gga, sentence)){
		}
		break;
	case MINMEA_SENTENCE_GSA:
	//case MINMEA_SENTENCE_GNGSA:
	//case MINMEA_SENTENCE_GPGSA:
	//case MINMEA_SENTENCE_BDGSA:
		if(minmea_parse_gsa(&info->gsa, sentence)){
		}
		break;
	case MINMEA_SENTENCE_GSV:
	//case MINMEA_SENTENCE_GPGSV:
	//case MINMEA_SENTENCE_BDGSV:
		if(true == minmea_parse_gsv(&info->gsv[info->ngsv], sentence)){
			info->ngsv++;
		}
		break;
	case MINMEA_SENTENCE_VTG:
		if(minmea_parse_vtg(&info->vtg, sentence)){
		}
		break;
	default:
	    break;
	}
}

/**************************************************************************************
* FunctionName   : gpsbuffer_to_gpsinfo()
* Description    : NMEA数据转换
* EntryParameter :
* ReturnValue    : None
**************************************************************************************/
void gpsbuffer_to_gpsinfo(struct gpsinfo *info, uint8_t *data, int16_t len)
{
	uint16_t bpos = 0, epos = 0;
	
	// 1.分离报文
	while(len > 0){
		// 1.1.获取报文起始偏移
		bpos = gps_begin(data, len);
		if(bpos >= len - 1){
			break;
		}
		
		// 1.2.将报文指针移动到起始位置
		GPS_MOVE_STEPS(data, len, bpos);
		
		// 1.3.获取报文尾部偏移
		epos = gps_end(data, len);
		if(epos >= len - 1){
			break;
		}
		
		// 1.4.检查报文长度是否正确
		if(epos > GPS_NMEA_MAXLEN){
			goto NEXT;
		}
		
		// 1.5.填充结束符
		GPS_FILL_EOF(data, epos);
		
		// 1.6.解析报文
		gps_parse(info, data);
		
		NEXT:
		
		// 1.7.将报文指针移动到结束位置
		GPS_MOVE_STEPS(data, len, epos);
	}
}
