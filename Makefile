src = $(wildcard *.c)
obj = $(src:.c=.o)

LIBS = -pthread

LDFLAGS = -lm

slp: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f $(obj) slp
