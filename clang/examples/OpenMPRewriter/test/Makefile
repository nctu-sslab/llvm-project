EXE= offload_array offload_malloc

all: $(EXE)

%: %.c.c
	clang -fopenmp -fopenmp-targets=nvptx64 $< -o $@

%.c.c:  %.c FORCE
	clang -fsyntax-only -Xclang -load -Xclang /home/pschen/llvm/latest/build/lib/OpenMPRewrite.so -Xclang -plugin -Xclang omp-rewtr $< -o $@ 

run:
	LIBOMPTARGET_DEBUG=1 ./offload

clean:
	rm -rf $(EXE)
