## Treebeard
## RTE_SDK := $(abspath /home/jcarlon/zzz_test/dpdk )
## Clpex
RTE_SDK := $(abspath /home/jcarlon/test/dpdk )

# RTE_SDK := $(abspath /home/carlon/test/dpdk )
RTE_TARGET ?= build
RTE_ARCH ?= x86_64-native-linuxapp-gcc

APP = latency_test
DEBUG ?= 0


CFLAGS += -O3 -march=native -msse3 -mssse3
CFLAGS += $(WERROR_FLAGS)
CFLAGS += -I$(RTE_SDK)/$(RTE_TARGET)/include

ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG -g -O0
endif

LDFLAGS += -L$(RTE_SDK)/$(RTE_TARGET)/lib
LDFLAGS += -L/usr/local/lib/x86_64-linux-gnu

SRCS-y := latency_test.c

PKGCONF ?= pkg-config

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)


# Add DPDK libraries
LDLIBS += -Wl,--whole-archive
LDLIBS += -lrte_net_ixgbe
#LDLIBS += -lrte_pmd_ixgbe
#LDLIBS += -lrte_pmd_ring
LDLIBS += -lrte_bus_vdev
LDLIBS += -lrte_bus_pci
LDLIBS += -lrte_net_i40e -lrte_bus_pci
LDLIBS += -Wl,--no-whole-archive
LDLIBS += -lrte_eal -lrte_mempool -lrte_mbuf
LDLIBS += -lrte_ethdev -lrte_net -lrte_hash
LDLIBS += -lrte_kvargs
LDLIBS += -lrte_cmdline -lrte_ring
LDLIBS += -lpthread -lm -ldl -lnuma
LDLIBS += -lrte_log

all: shared
	@echo "Building $(APP)"
	@$(CC) $(CFLAGS) $(SRCS-y) -o $(APP) $(LDFLAGS) $(LDLIBS)

debug:
	$(MAKE) DEBUG=1

shared:
	@echo "Using DPDK from $(RTE_SDK)"
	@if [ ! -d "$(RTE_SDK)/$(RTE_TARGET)" ]; then \
		echo "DPDK not built - please compile DPDK first"; \
		exit 1; \
	fi

clean:
	rm -f $(APP)
