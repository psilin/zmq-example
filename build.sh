#!/bin/sh

#server
g++ ./zmq_server.cpp -o zmq_server -lzmq -pthread

#client
g++ ./zmq_client.cpp -o zmq_client -lzmq