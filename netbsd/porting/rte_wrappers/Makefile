RTE_SDK=$(CURRENT_DIR)dpdk-2.0.0
RTE_TARGET ?= x86_64-native-linuxapp-gcc
include $(RTE_SDK)/mk/rte.vars.mk
SRC_ROOT=$(CURRENT_DIR)
SRCS-y := rte_wrappers.c
CFLAGS += $(OPTIMIZATION)
#CFLAGS += -g
CFLAGS += $(WERROR_FLAGS)
DPDK_HEADERS = $(SRC_ROOT)/dpdk-2.0.0/x86_64-native-linuxapp-gcc/include
ALL_HEADERS = -I$(DPDK_HEADERS) -I$(SRC_ROOT)
CFLAGS += $(ALL_HEADERS) -DRTE_MAX_LCORE=32 -DAPP_MBUFS_POOL_SIZE=16384
#-DGSO
#-D_KERNEL
#-DMSIZE=256
#-D_NETBSD_SOURCE
#-D__NetBSD__
#-DINET
SHARED = librtewrappers.so
include $(RTE_SDK)/mk/rte.extshared.mk
