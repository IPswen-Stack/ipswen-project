all: fakedns.so test_client test_server test_client_legacy

fakedns.so: fakedns.cpp patch.h
	g++ -shared -fPIC -O2 -o fakedns.so fakedns.cpp -ldl -DENABLE_DEBUG

test_client: test_client.cpp
	g++ -o test_client test_client.cpp

test_server: test_server.cpp
	g++ -o test_server test_server.cpp

test_client_legacy: test_client_legacy.cpp
	g++ -o test_client_legacy test_client_legacy.cpp