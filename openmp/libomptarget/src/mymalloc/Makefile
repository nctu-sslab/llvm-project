TARGETS=libmymalloc.so
all: $(TARGETS)

run: libmymalloc.so main
	export LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):.
	LD_PRELOAD="libmymalloc.so" ./main

libmymalloc.so: mymalloc.cpp mem_layout.cpp mmap_mgr.cpp
	clang $^ -shared -fPIC -o $@  -ldl -ldl -lrt -lstdc++ -I ../ -I ../../include -I.






main: main.c
	clang $^  -o main

clean:
	rm -rf $(TARGETS) *.o

	#-I/home/pschen/src-pschen/src/build -Debug/include -I /home/pschen/sslab/src-pschen/omp_offloading/include -L /home/pschen/src-pschen/src/build-Debug/lib -lomptarget  -O2 -DN=10    2D.c   -o 2D
