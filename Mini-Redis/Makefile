server: server.o shared.o
	g++ -o server server.o shared.o -pthread

server.o: server.cpp shared.h
	g++ -c server.cpp

shared.o: shared.cpp shared.h
	g++ -c shared.cpp

run: server
	./server

clean:
	rm -f *.o server