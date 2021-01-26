APPNAME=test
CC=gcc

PKG_DEPS=glfw3 glew gstreamer-1.0 gstreamer-app-1.0
CFLAGS=-std=gnu99 -O1 -ggdb -Wall $(shell pkg-config --cflags $(PKG_DEPS))

OS := $(shell uname)
ifeq ($(OS),Darwin)
	LDFLAGS_OS=-framework OpenGL
else
	LDFLAGS_OS=-lGL
endif

LDFLAGS=$(LDFLAGS_OS) \
		-lavcodec \
		-lavformat \
		-lavutil \
		-lpthread \
		$(shell pkg-config --libs $(PKG_DEPS))

CFILES = \
		 bmp_loader.c \
		 pipeline_src.c \
		 pipeline_proc_defish.c \
		 pipeline_sink_gst.c \
		 qlib.c \
		 winsys_glfw.c

OBJFILES=$(patsubst %.c,%.o,$(CFILES))

all: $(APPNAME)

$(APPNAME): $(OBJFILES)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJFILES)

$(OBJFILES): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm $(APPNAME) *.o || true

run:
	make clean
	make all
	./$(APPNAME)
