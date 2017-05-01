# Copyright 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include

LOCAL_MODULE := libvmodule

LOCAL_MODULE_TAGS := optional

TARGET_ARCH_ABI := arm64-v8a

LOCAL_MULTILIB := 64

VMODULE_CPP_LIST := \
    $(wildcard $(LOCAL_PATH)/*.cpp) \
    $(wildcard $(LOCAL_PATH)/threads/*.cpp) \
    $(wildcard $(LOCAL_PATH)/messaging/*.cpp) \
    $(wildcard $(LOCAL_PATH)/utils/*.cpp) \
    $(wildcard $(LOCAL_PATH)/vutils/*.cpp)


$(warning $(VMODULE_CPP_LIST))

LOCAL_SRC_FILES := \
    $(subst $(LOCAL_PATH)/, , $(VMODULE_CPP_LIST))
			
$(warning $(LOCAL_SRC_FILES))
						
LOCAL_SHARED_LIBRARIES := \
	libutils \
	libcutils \
	liblog

#-c -fmessage-length=0 -O3 -Wall -Wno-unused-parameter -fexceptions -DTARGET_ANDROID -D_DEBUG -std=gnu++11
LOCAL_CFLAGS := -O3 -Wall -Wno-unused-parameter -fexceptions -DTARGET_ANDROID -D_DEBUG -std=c++11 -fmessage-length=0

LOCAL_CXXFLAGS := $(LOCAL_CFLAGS)

LOCAL_LDLIBS := -pthread -lm -llog

include $(BUILD_SHARED_LIBRARY)
