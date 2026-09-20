
CC       ?= cc
BUILD_DIR = build

INCLUDES  = -Iconfig -Imatrix -Imultiplying -Itimer -Ivendor
WARNINGS  = -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CFLAGS    = -std=c11 -O3 -march=native -fno-math-errno -ffp-contract=off $(INCLUDES) $(WARNINGS)
LDLIBS    = -lm

NOVEC_CFLAGS = -fno-vectorize -fno-slp-vectorize -fno-tree-vectorize

VECTORIZATION_REPORT_FLAGS = -Rpass=loop-vectorize -Rpass-missed=loop-vectorize \
                             -Rpass=slp-vectorizer

TARGET  = lab1
OBJECTS = $(BUILD_DIR)/main.o \
          $(BUILD_DIR)/matrix.o \
          $(BUILD_DIR)/timer.o \
          $(BUILD_DIR)/multiply_auto.o \
          $(BUILD_DIR)/multiply_scalar.o \
          $(BUILD_DIR)/multiply_sse2.o

.PHONY: all run report disasm prove x86 x86-run x86-disasm clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/multiply_scalar.o: multiplying/multiply_scalar.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(NOVEC_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: matrix/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: timer/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: multiplying/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

report:
	$(CC) $(CFLAGS) $(VECTORIZATION_REPORT_FLAGS) -c -o /dev/null multiplying/multiply_auto.c
	$(CC) $(CFLAGS) $(NOVEC_CFLAGS) $(VECTORIZATION_REPORT_FLAGS) -c -o /dev/null multiplying/multiply_scalar.c

disasm: $(OBJECTS)
	@for object in multiply_auto multiply_scalar multiply_sse2; do \
		printf '%-20s vector(.2d)=%-5s scalar(d)=%-5s fma=%s\n' "$$object.o" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -cE '\b(fmul|fadd)\.2d\b')" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -cE '\b(fmul|fadd)[[:space:]]+d[0-9]+')" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -cE '\b(fmla|fmls|fmadd|fmsub)')"; \
	done


# Which of the two vectorizers actually produces the vector code. Compiles the same
# source four times and counts packed-double instructions in each object.
prove: | $(BUILD_DIR)
	@printf '%-36s %14s %14s\n' 'flags' 'vector(.2d)' 'scalar(d)'
	@for flags in '' '-fno-vectorize' '-fno-slp-vectorize' '-fno-vectorize -fno-slp-vectorize'; do \
		$(CC) $(CFLAGS) $$flags -c -o $(BUILD_DIR)/prove.o multiplying/multiply_auto.c; \
		printf '%-36s %14s %14s\n' "$${flags:-(none, default -O3)}" \
			"$$(objdump -d $(BUILD_DIR)/prove.o | grep -cE '\b(fmul|fadd)\.2d\b')" \
			"$$(objdump -d $(BUILD_DIR)/prove.o | grep -cE '\b(fmul|fadd)[[:space:]]+d[0-9]+')"; \
	done
	@rm -f $(BUILD_DIR)/prove.o


X86_BUILD_DIR = $(BUILD_DIR)/x86
X86_TARGET    = $(X86_BUILD_DIR)/lab1_x86
X86_CFLAGS    = -arch x86_64 -std=c11 -O3 -msse2 -fno-math-errno -ffp-contract=off \
                $(INCLUDES) $(WARNINGS)
X86_OBJECTS   = $(X86_BUILD_DIR)/main.o \
                $(X86_BUILD_DIR)/matrix.o \
                $(X86_BUILD_DIR)/timer.o \
                $(X86_BUILD_DIR)/multiply_auto.o \
                $(X86_BUILD_DIR)/multiply_scalar.o \
                $(X86_BUILD_DIR)/multiply_sse2.o

x86: $(X86_TARGET)

$(X86_BUILD_DIR):
	mkdir -p $(X86_BUILD_DIR)

$(X86_TARGET): $(X86_OBJECTS)
	$(CC) -arch x86_64 -o $@ $(X86_OBJECTS) $(LDLIBS)

$(X86_BUILD_DIR)/multiply_scalar.o: multiplying/multiply_scalar.c | $(X86_BUILD_DIR)
	$(CC) $(X86_CFLAGS) $(NOVEC_CFLAGS) -c -o $@ $<

$(X86_BUILD_DIR)/main.o: main.c | $(X86_BUILD_DIR)
	$(CC) $(X86_CFLAGS) -c -o $@ $<

$(X86_BUILD_DIR)/%.o: matrix/%.c | $(X86_BUILD_DIR)
	$(CC) $(X86_CFLAGS) -c -o $@ $<

$(X86_BUILD_DIR)/%.o: timer/%.c | $(X86_BUILD_DIR)
	$(CC) $(X86_CFLAGS) -c -o $@ $<

$(X86_BUILD_DIR)/%.o: multiplying/%.c | $(X86_BUILD_DIR)
	$(CC) $(X86_CFLAGS) -c -o $@ $<

x86-run: $(X86_TARGET)
	@pgrep -q oahd || { \
		echo "Rosetta is not running; install it with: softwareupdate --install-rosetta"; \
		exit 1; \
	}
	@echo "running the x86_64 build under Rosetta - timings are translated, do not compare them with the native run"
	@echo
	@./$(X86_TARGET); status=$$?; \
	if [ $$status -ne 0 ]; then \
		echo; \
		echo "note: only the two bit-exact checks above carry meaning here."; \
		echo "      Rosetta translation adds a few percent of noise, so the timing"; \
		echo "      requirement flips between runs. Judge it on the native build."; \
	fi

# Real SSE2 instructions in the x86 objects, next to the native NEON ones. Counts
# are per block product; the mnemonic each count refers to is in parentheses, since
# the two architectures spell the same operation differently.
x86-disasm: $(X86_OBJECTS) $(OBJECTS)
	@printf '%-22s %-14s %-17s %-17s %-21s %s\n' \
		'object' 'arch' 'multiply' 'add' 'scalar' 'fma'
	@for object in multiply_auto multiply_scalar multiply_sse2; do \
		printf '%-22s %-14s %-17s %-17s %-21s %s\n' "$$object.o" 'x86_64 SSE2' \
			"$$(objdump -d $(X86_BUILD_DIR)/$$object.o | grep -c 'mulpd') (mulpd)" \
			"$$(objdump -d $(X86_BUILD_DIR)/$$object.o | grep -c 'addpd') (addpd)" \
			"$$(objdump -d $(X86_BUILD_DIR)/$$object.o | grep -cE 'mulsd|addsd') (mulsd/addsd)" \
			"$$(objdump -d $(X86_BUILD_DIR)/$$object.o | grep -cE 'vfmadd|vfmsub') (vfmadd)"; \
		printf '%-22s %-14s %-17s %-17s %-21s %s\n' "" 'aarch64 NEON' \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -c 'fmul\.2d') (fmul.2d)" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -c 'fadd\.2d') (fadd.2d)" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -cE 'fmul[[:space:]]+d[0-9]+|fadd[[:space:]]+d[0-9]+') (fmul d/fadd d)" \
			"$$(objdump -d $(BUILD_DIR)/$$object.o | grep -cE 'fmla|fmadd') (fmla/fmadd)"; \
	done
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# objdump -d build/multiply_sse2.o  .2d 
# objdump -d build/multiply_scalar.o а здесь не будет .2d