CC = /bin/g++
EXE = np_simple np_multi_proc np_single_proc

all:
	$(CC) np_simple.cpp      -o np_simple
	$(CC) np_single_proc.cpp -o np_single_proc
	$(CC) np_multi_proc.cpp  -pthread -o np_multi_proc

clean:
	rm $(EXE)
