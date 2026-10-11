# Lab 2 - cache-optimised matrix multiplication. Release-only, as the lab requires.

CC       ?= cc
BUILD_DIR = build

INCLUDES  = -Iconfig -Icache -Imatrix -Imultiplying -Itimer -Ivendor
WARNINGS  = -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CFLAGS    = -std=c11 -O3 -march=native -fno-math-errno -ffp-contract=off $(INCLUDES) $(WARNINGS)
LDLIBS    = -lm

# C0 is built without vectorisation. Both switches are needed: the 10-element loop is
# unrolled before the loop vectoriser runs, so the SLP vectoriser would vectorise it.
NOVEC_CFLAGS = -fno-vectorize -fno-slp-vectorize

# make FMA=1 switches every kernel to fused multiply-add (make clean first).
FMA ?= 0
ifeq ($(FMA),1)
CFLAGS += -DLAB2_FUSED_MULTIPLY_ADD
endif

TARGET  = lab2
OBJECTS = $(BUILD_DIR)/main.o \
          $(BUILD_DIR)/cache.o \
          $(BUILD_DIR)/matrix.o \
          $(BUILD_DIR)/timer.o \
          $(BUILD_DIR)/multiply_scalar.o \
          $(BUILD_DIR)/multiply_vectorized.o \
          $(BUILD_DIR)/multiply_blocked.o

# Load latency by working-set size.
PROBE_TARGET  = cache_probe
PROBE_OBJECTS = $(BUILD_DIR)/cache_probe.o $(BUILD_DIR)/cache.o $(BUILD_DIR)/timer.o

.PHONY: all run probe disasm clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/multiply_scalar.o: multiplying/multiply_scalar.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(NOVEC_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: cache/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: matrix/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: timer/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: multiplying/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: probe/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROBE_TARGET): $(PROBE_OBJECTS)
	$(CC) -o $@ $(PROBE_OBJECTS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

probe: $(PROBE_TARGET)
	./$(PROBE_TARGET)

# Arithmetic instructions per kernel: C0 must be scalar only, C1 and C2 vector only,
# and all of one kind (separate or fused) -- that keeps them bit-exact.
disasm: $(OBJECTS)
	@printf '%-24s %8s %8s %8s %8s %8s %8s\n' 'object' 'fmul.2d' 'fadd.2d' 'fmla.2d' 'fmul d' 'fadd d' 'fmadd d'
	@for object in multiply_scalar multiply_vectorized multiply_blocked; do \
		listing="$$(objdump -d $(BUILD_DIR)/$$object.o)"; \
		printf '%-24s %8s %8s %8s %8s %8s %8s\n' "$$object.o" \
			"$$(printf '%s\n' "$$listing" | grep -c 'fmul\.2d')" \
			"$$(printf '%s\n' "$$listing" | grep -c 'fadd\.2d')" \
			"$$(printf '%s\n' "$$listing" | grep -c 'fmla\.2d')" \
			"$$(printf '%s\n' "$$listing" | grep -cE 'fmul[[:space:]]+d[0-9]')" \
			"$$(printf '%s\n' "$$listing" | grep -cE 'fadd[[:space:]]+d[0-9]')" \
			"$$(printf '%s\n' "$$listing" | grep -cE 'fmadd[[:space:]]+d[0-9]')"; \
	done

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(PROBE_TARGET)
