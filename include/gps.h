#ifndef _GPS_H_
#define _GPS_H_

#include "minmea.h"

struct gpsinfo {
	int ngsv;
	struct minmea_sentence_rmc rmc;
	struct minmea_sentence_gsa gsa;
	struct minmea_sentence_gsv gsv[5];
	struct minmea_sentence_gga gga;
	struct minmea_sentence_vtg vtg;
};

/**************************************************************************************
* FunctionName   : gpsbuffer_to_gpsinfo()
* Description    : NMEA数据转换
* EntryParameter :
* ReturnValue    : None
**************************************************************************************/
void gpsbuffer_to_gpsinfo(struct gpsinfo *info, uint8_t *data, int16_t len);

#endif
