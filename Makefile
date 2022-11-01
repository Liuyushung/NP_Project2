CC = /bin/g++
EXE = np_simple np_multi_proc np_single_proc
OBJ = np_multi_proc.cpp  np_simple.cpp  np_single_proc.cpp

all:
	$(CC) np_simple.cpp      -o np_simple
	$(CC) np_multi_proc.cpp  -o np_multi_proc
	$(CC) np_single_proc.cpp -o np_single_proc

clean:
	rm $(EXE)
