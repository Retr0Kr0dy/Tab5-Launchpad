CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
OUTDIR := out
OBJS := test_registry.o
all: $(OUTDIR)/test_sic_native
$(OUTDIR)/test_sic_native: $(OBJS)
	mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $^
clean:
	rm -rf $(OUTDIR) *.o
