CC := gcc
CFLAGS := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2 -Iinclude
LDFLAGS :=

BIN_DIR := bin
CORE_OBJS := \
	src/app.o \
	src/cmdu.o \
	src/ethernet.o \
	src/raw_socket.o \
	src/tlv.o

TARGETS := \
	$(BIN_DIR)/easymesh-controller \
	$(BIN_DIR)/easymesh-agent

LEGACY_TARGETS := controller_rx Topology_Discovery_a1_to_c

.PHONY: all clean

all: $(TARGETS)



$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/easymesh-controller: apps/controller.o $(CORE_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN_DIR)/easymesh-agent: apps/agent.o $(CORE_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	$(RM) $(TARGETS) apps/*.o src/*.o
