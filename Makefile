TARGETS = pbserial

CPPFLAGS += -I./ -I../        
LDFLAGS += -L./ -lpthread 

SRC_FILES = ${wildcard *.c}
all: $(TARGETS)

pbserial: protobuf
	echo "Compile pbserial"
	$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) $(SRC_FILES)
	$(LINK.o) *.o $(LDFLAGS) $(USR_LIB) protobuf-c/*.o -o $@

protobuf:
	(cd protobuf-c/;\
		rm -rf data.pb-c.*; \
		protoc-c --c_out=. data.proto; \
		$(COMPILE.c) $(CPPFLAGS) $(LDFLAGS) protobuf-c.c data.pb-c.c)

clean:
	rm -rf $(TARGETS) *.o
	-@rm -rf protobuf-c/data.pb-c.*
	-@rm -rf protobuf-c/*.o
