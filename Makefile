CC=/usr/bin/g++
CC_OPTS= -Wall -std=c++11
CC_LIBS=-pthread
CC_DEFINES=
CC_INCLUDES=
CC_ARGS=${CC_OPTS} ${CC_LIBS} ${CC_DEFINES} ${CC_INCLUDES}

.PHONY=clean

all: reliable_sender reliable_receiver

reliable_sender: sender_main.cpp
	@${CC} ${CC_ARGS} -o reliable_sender sender_main.cpp

reliable_receiver: receiver_main.cpp
	@${CC} ${CC_ARGS} -o reliable_receiver receiver_main.cpp

clean:
	@rm -f reliable_sender reliable_receiver *.o
