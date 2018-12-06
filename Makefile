
all: logSol

logSol: logSol.c histo.c
	gcc -o logSol logSol.c histo.c
