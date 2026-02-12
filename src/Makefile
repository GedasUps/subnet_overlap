OBJS = main.o ubus.o netlink.o
TARGET = subnet_overlap

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lubus -lubox -lnl-tiny

%.o: %.c
	$(CC) $(CFLAGS) -I$(STAGING_DIR)/usr/include/libnl-tiny -c -o $@ $<

clean:
	rm -f $(TARGET) *.o