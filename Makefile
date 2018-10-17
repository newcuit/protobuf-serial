TARGETS = pbserial
PROTO_DIR := protobuf-c
EMAP_DIR := emap
GPS_DIR := gps

CPPFLAGS += -O3 -g -I./ -I../  -Iinclude
LD_FLAGS += -Wl,--no-as-needed -std=c++11 -L./ -L./lib -ladasisHP -lpthread -lm -lstdc++

SRC_FILES = ${wildcard *.c}
EMAP_FILES = ${wildcard $(EMAP_DIR)/*.c}
GPS_FILES = ${wildcard $(GPS_DIR)/*.c}
PROTO_FILES = $(PROTO_DIR)/protobuf-c.c $(PROTO_DIR)/data.pb-c.c

all: $(TARGETS)

pbserial: protobuf oemap onmea
	-@echo ""
	-@echo "Compile pbserial"
	$(CC) -c $(CPPFLAGS) $(SRC_FILES)
	-@echo ""
	-@echo "Link pbserial"
	$(CC) *.o  $(LD_FLAGS) -o $@

oemap:
	-@echo ""
	-@echo "Compile emap"
	$(CC) -c $(CPPFLAGS) $(EMAP_FILES)

onmea:
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
