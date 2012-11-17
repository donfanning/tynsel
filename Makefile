ifneq ($(DEBUG),)
CFLAGS += -g -O0
CPPFLAGS += -DVERBOSE
endif

fft: CPPFLAGS += -std=c99
fft: LDLIBS += -lfftw3 -lsndfile

all: fft

clean:
	rm -f *.o fft
