CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall

all: lab generate addresses.txt

lab: main.cpp
	$(CXX) $(CXXFLAGS) -o lab main.cpp

generate: generate_addresses.cpp
	$(CXX) $(CXXFLAGS) -o generate generate_addresses.cpp

addresses.txt: generate
	./generate

run: lab addresses.txt
	./lab

clean:
	rm -f lab generate addresses.txt

.PHONY: all run clean
