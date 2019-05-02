CFLAGS += -O3 -g3  -W
LIBS += -lm -pthread
CC = gcc

worker:  receive.o
	$(CC) $(CFLAGS) receive.o  -o worker $(LIBS)

worker.o: receive.c
	$(CC) $(CFLAGS) -c receive.c 


host: main.o
clean:
	rm  -rf *.o host worker

mem_check: worker
	valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all -v ./worker 2

time_check: worker
	time --verbose ./worker $(threads_num)

