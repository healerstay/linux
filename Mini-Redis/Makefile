server: server.o utils.o shared.o
	g++ -o server server.o utils.o shared.o -pthread

server.o: server.cpp
	g++ -c server.cpp

utils.o: utils.cpp
	g++ -c utils.cpp

shared.o: shared.cpp
	g++ -c shared.cpp

run: server
	./server

clean:
	rm -f *.o server