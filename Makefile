OPTIONS = -g -rdynamic -ggdb -O0
D_FLAGS =
C_FLAGS = -Wall $(OPTIONS) $(D_FLAGS) -I../sendao -std=c++11
L_FLAGS = -L../sendao -Wall $(OPTIONS)
LIBS = -lsendao


CLIOBJS = deploy.o pipe.o scripts.o

deploy: $(CLIOBJS)
	rm -f deploy
	g++ $(L_FLAGS) -o deploy $(CLIOBJS) $(LIBS)
	cp -f deploy bin/

clean:
	rm -f $(CLIOBJS)
	rm -f deploy

.cpp.o:
	g++ -c $(C_FLAGS) $<
	
