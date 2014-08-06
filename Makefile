OBJ=
CPBIN=el-client el-send

OPTIMIZATION=-O3
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings
DEBUG= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CFLAGS) $(WARNINGS) $(DEBUG) $(ARCH)
REAL_LDFLAGS=$(LDFLAGS) $(ARCH)


all: $(CPBIN)

el-client.o: el-client.c cpapp_config.h cpapp_helper.h

el-send.o: el-send.c cpapp_config.h cpapp_helper.h

cpapp_helper.o: cpapp_helper.c cpapp_config.h cpapp_helper.h

el-client: el-client.o cpapp_helper.o
	$(CC) -o $@ $(REAL_CFLAGS) $(REAL_LDFLAGS) el-client.o cpapp_helper.o

el-send: el-send.o cpapp_helper.o
	$(CC) -o $@ $(REAL_CFLAGS) $(REAL_LDFLAGS) el-send.o cpapp_helper.o

cpbin: $(CPBIN)

.c.o:
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<

clean:
	rm -rf $(CPBIN) *.o

