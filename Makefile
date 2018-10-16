TARGETS = pbserial
PROTO_DIR := protobuf-c
EMAP_DIR := emap
GPS_DIR := gps

CPPFLAGS += -Wl,--no-as-needed -I./ -I../  -Iinclude
LDFLAGS += -L./ -L./lib -ladasisHP -lpthread -lstdc++ -lm

SRC_FILES = ${wildcard *.c}
EMAP_FILES = ${wildcard $(EMAP_DIR)/*.c}
GPS_FILES = ${wildcard $(GPS_DIR)/*.c}
PROTO_FILES = $(PROTO_DIR)/protobuf-c.c $(PROTO_DIR)/data.pb-c.c

all: $(TARGETS)

pbserial: protobuf oemap onmea
	-@echo "Compile pbserial"
	$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) $(SRC_FILES)
	$(LINK.o) *.o $(LDFLAGS) -o $@

oemap:
	-@echo "Compile emap"
	$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) $(EMAP_FILES)

onmea:
	-@echo "Compile gps"
	$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) $(GPS_FILES)

protobuf:
	-@echo "Compile protobuf"
	-@rm -rf $(PROTO_DIR)/data.pb-c.*
	protoc-c --c_out=. $(PROTO_DIR)/data.proto

	$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) $(PROTO_FILES)

clean:
	rm -rf $(TARGETS) *.o
	-@rm -rf $(PROTO_DIR)/data.pb-c.*
