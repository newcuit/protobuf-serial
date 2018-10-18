SDK_PATH   ?= $(shell pwd)/../..

TARGETS = pbserial
PROTO_DIR := protobuf-c
EMAP_DIR := emap
GPS_DIR := gps
AUDIO_DIR := audio

CPPFLAGS += -O3 -g -I./ -I../  -Iinclude -I./inc -I../../include
CPPFLAGS += -I$(SDK_PATH)/lib/interface/inc           \
			-I$(SDKTARGETSYSROOT)/usr/include         \
			-I$(SDKTARGETSYSROOT)/usr/include         \
			-I$(SDKTARGETSYSROOT)/usr/include/data    \
			-I$(SDKTARGETSYSROOT)/usr/include/dsutils \
			-I$(SDKTARGETSYSROOT)/usr/include/qmi     \
			-I$(SDKTARGETSYSROOT)/usr/include/qmi-framework

LD_FLAGS += -Wl,--no-as-needed -std=c++11 -L./lib 
LD_FLAGS += -ladasisHP -lpthread -lm -lstdc++
LD_FLAGS += -lql_lib_audio -llog -L$(SDK_PATH)/lib -lrt

SRC_FILES = ${wildcard *.c}
EMAP_FILES = ${wildcard $(EMAP_DIR)/*.c}
GPS_FILES = ${wildcard $(GPS_DIR)/*.c}
AUDIO_FILES = ${wildcard $(AUDIO_DIR)/*.c}
PROTO_FILES = $(PROTO_DIR)/protobuf-c.c $(PROTO_DIR)/data.pb-c.c

all: $(TARGETS)

pbserial: protobuf o_nmea o_emap o_audio
	-@echo ""
	-@echo "Compile pbserial"
	$(CC) -c $(CPPFLAGS) $(SRC_FILES)
	-@echo ""
	-@echo "Link pbserial"
	$(CC) *.o  $(LD_FLAGS) -o $@

o_audio:
	-@echo ""
	-@echo "Compile audio"
	$(CC) -c $(CPPFLAGS) $(AUDIO_FILES)

o_emap:
	-@echo ""
	-@echo "Compile emap"
	$(CC) -c $(CPPFLAGS) $(EMAP_FILES)

o_nmea:
	-@echo ""
	-@echo "Compile gps"
	$(CC) -c $(CPPFLAGS) $(GPS_FILES)

protobuf:
	-@echo ""
	-@echo "Compile protobuf"
	-@rm -rf $(PROTO_DIR)/data.pb-c.*
	protoc-c --c_out=. $(PROTO_DIR)/data.proto

	$(CC) -c $(CPPFLAGS) $(PROTO_FILES)

clean:
	rm -rf $(TARGETS) *.o
	-@rm -rf $(PROTO_DIR)/data.pb-c.*
