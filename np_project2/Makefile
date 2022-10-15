CC = g++

all:np_simple np_single_proc np_multi_proc

np_simple:np_simple.cpp
	$(CC) np_simple.cpp -o np_simple

np_single_proc:np_single_proc.cpp
	$(CC) np_single_proc.cpp -o np_single_proc

np_multi_proc:np_multi_proc.cpp
	$(CC) np_multi_proc.cpp -o np_multi_proc

clean:
	rm np_simple
	rm np_single_proc
	rm np_multi_proc
