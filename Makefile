CC := clang
C++ := clang++
C_FLAGS := -Iinclude/ -ggdb
C_WARNINGS := \
		-Werror -Wall -Wextra -Wcast-align -Wcast-qual -Wchar-subscripts \
		-Wdisabled-optimization -Wformat-nonliteral -Wformat-security \
		-Wformat-y2k -Wimport -Winit-self -Winvalid-pch -Wmissing-braces \
		-Wmissing-field-initializers -Wmissing-format-attribute \
		-Wmissing-include-dirs -Wpacked -Wparentheses -Wpointer-arith \
		-Wredundant-decls -Wreturn-type -Wsequence-point -Wshadow \
		-Wsign-compare -Wswitch -Wtrigraphs -Wunused -Wno-unused-function \
		-Wunused-label -Wunused-parameter -Wunused-value -Wunused-variable \
		-Wvariadic-macros -Wvolatile-register-var -Wwrite-strings -Wundef \
		-Wmultichar -Wconversion -Wsign-conversion -Wmissing-noreturn \
		-Wuninitialized -Wswitch-enum
CPP_FLAGS := --std=c++11 $(shell pkg-config --cflags Magick++)
LD_FLAGS := -lsqlite3 -lmicrohttpd -lexiv2 $(shell pkg-config --libs Magick++)

BASE_SRC = $(wildcard src/*.cpp) $(wildcard lib/*.c) $(wildcard src/*.c)
BASE_OBJS = $(patsubst %.cpp,%.o,$(patsubst %.c,%.o,${BASE_SRC}))
BASE_DEPS = $(patsubst %.cpp,%.d,$(patsubst %.c,%.d,${BASE_SRC}))

WEB_RESOURCES := $(filter-out $(wildcard web/*.o),$(wildcard web/*))
WEB_OBJS := $(patsubst web/%,web/%.o,${WEB_RESOURCES})

all:	webserver slide

webserver:	main/webserver.o ${BASE_OBJS} ${WEB_OBJS}
	${C++} ${LD_FLAGS} -o $@ $+

# These files all exist and are the data for the web interface.
${WEB_RESOURCES}:;

		# add --prefix-symbols=
web/%.o:	web/%
		ld -r -o $@ -z noexecstack --format=binary $<
		objcopy \
            --rename-section .data=.rodata,alloc,load,readonly,data,contents \
			$@

slide:	main/slide.o ${BASE_OBJS}
	${C++} ${C_FLAGS} ${CPP_FLAGS} -o $@ $+ ${LD_FLAGS}

%.o:	%.cpp
	${C++} -c ${C_FLAGS} ${C_WARNINGS} ${CPP_FLAGS} -o $@ $<

lib/%.o:	lib/%.c
	${CC} -c ${C_FLAGS} -o $@ $<

src/%.o:	lib/%.c
	${CC} -c ${C_FLAGS} ${C_WARNINGS} -o $@ $<

clean:
	rm -f main/*.d main/*.o ${BASE_DEPS} ${BASE_OBJS} ${WEB_OBJS}

.PHONY:	clean

distclean:	clean
	rm -f slide webserver

.PHONY:	distclean

