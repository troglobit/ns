EXEC   := ns
SRCS   := ns.c
OBJS   := ns.o
CFLAGS += -g -Og -W -Wall -Wextra -Wno-unused-parameter

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

clean:
	$(RM) $(EXEC) $(OBJS)

distclean: clean
	$(RM) *.o *~ *.bak
