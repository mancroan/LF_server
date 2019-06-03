
all:
	gcc -o server1 server.c -lpthread -O2
	gcc -o server2 echoserver.c -lpthread -O2
	gcc -o test_long_conn test_long_conn.c -O2
	gcc -o test_short_conn test_short_conn.c -O2

clean:
	rm -f server test_long_conn test_short_conn