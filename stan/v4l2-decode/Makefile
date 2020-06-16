# V4L2 Codec decoding example application
# Kamil Debski <k.debski@samsung.com>
#
# Copyright 2012 Samsung Electronics Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

TCPATH = aarch64-linux-gnu-

CC = ${TCPATH}gcc

INCLUDES = 

SOURCES = main.c args.c parser.c video.c ctrls.c tracer.c
OBJECTS := $(SOURCES:.c=.o)
EXEC = v4l2_decode
CFLAGS = -Wall -g -std=gnu99
LIBS = -lpthread
LIBPATH = -L/usr/lib/aarch64-linux-gnu
LDFLAGS = -o $(EXEC) $(LIBPATH) $(LIBS)

all: $(EXEC)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $<

$(EXEC): $(OBJECTS)
	$(CC) -o $(EXEC) $(OBJECTS) $(LIBS)

clean:
	rm -f *.o $(EXEC)

install:

.PHONY: clean all
