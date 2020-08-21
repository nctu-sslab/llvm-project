CUDA=libuvmmalloc.so
TARGETS=libmymalloc.so
all: $(CUDA)

run: libmymalloc.so main
	export LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):.
	LD_PRELOAD="libmymalloc.so" ./main

libmymalloc.so: mymalloc.cpp mem_layout.cpp mmap_mgr.cpp
	clang $^ -shared -fPIC -o $@  -ldl -ldl -lrt -lstdc++ -I ../ -I ../../include -I.


libuvmmalloc.so: uvm_malloc.cu
	clang $^ -shared -fPIC -o $@  -lstdc++ -I. --cuda-gpu-arch=sm_60 -lcuda -L/opt/cuda/lib  -Wl,-rpath,/opt/cuda/lib




main: main.c
	clang $^  -o main

clean:
	rm -rf $(TARGETS) $(CUDA) *.o

	#-I/home/pschen/src-pschen/src/build -Debug/include -I /home/pschen/sslab/src-pschen/omp_offloading/include -L /home/pschen/src-pschen/src/build-Debug/lib -lomptarget  -O2 -DN=10    2D.c   -o 2D
