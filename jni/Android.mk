LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/..

SOURCES_C :=

# 7zip
CFLAGS += -DHAVE_7ZIP -D_7ZIP_ST

# CHD
HAVE_CHD = 1

include $(CORE_DIR)/Makefile.common

COREFLAGS := -DANDROID -D__LIBRETRO__ -DUSE_LIBRETRO_VFS $(CFLAGS) $(INCFLAGS) -I$(LOCAL_PATH)/include

# 68020+ JIT on 32-bit ARM (armeabi-v7a), using the ARM backend shared with Amiberry/UAE4ARM.
# The ARM opcode tables (jit/compemu.c, compstbl.c, comptbl.h) must be generated first with
# jit/gencomp_arm.c (see .github/workflows/android.yml). Disable with PUAE_JIT=0.
PUAE_JIT ?= 1
JIT_SOURCES :=
ifeq ($(TARGET_ARCH_ABI)$(PUAE_JIT),armeabi-v7a1)
	# UAE must be known before sysdeps.h in the JIT sources; HAVE_MMAN enables mmap/mprotect.
	# No FPU JIT on ARM (the backend only has stubs): FPU opcodes stay in the interpreter.
	COREFLAGS += -DJIT -DCPU_arm -DARMV6_ASSEMBLY -DARM_ASSEMBLY -DUAE= -DHAVE_MMAN
	JIT_SOURCES := \
		$(EMU)/jit/compemu.c \
		$(EMU)/jit/compstbl.c \
		$(EMU)/jit/compemu_support.cpp
endif

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C) $(JIT_SOURCES)
LOCAL_CONLYFLAGS := -std=gnu99
LOCAL_CFLAGS    := $(COREFLAGS)
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/libretro/link.T
LOCAL_LDLIBS    := -lz
LOCAL_DISABLE_FORMAT_STRING_CHECKS := true
LOCAL_ARM_MODE  := arm
include $(BUILD_SHARED_LIBRARY)
