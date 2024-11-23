all: nLogSol

nLogSol: nLogSol.cpp
	g++ -o nLogSol -lm -l pthread  -Wno-write-strings nLogSol.cpp
