#!/bin/bash
g++ -c ./proto_desc.cpp -o proto.o
g++ -c ./message_encoder.cpp -o message.o
g++ -c ./encoder.cpp -o encoder.o
g++ message.o ./proto.o ./encoder.o
./a.out