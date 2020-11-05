#Name of the project
PROJ_NAME=ep2
 
# .c files
C_SOURCE=$(wildcard *.c)
 
# .h files
H_SOURCE=$(wildcard *.h)
 
# Object files
OBJ=$(subst .c,.o,$(subst source,objects,$(C_SOURCE)))
 
# Compiler and linker
CC=gcc
 
# Flags for compiler
CC_FLAGS=-c         \
		 -W         \
		 -Wall      \
		 -ansi      \
		 -pedantic
 
# Command used at clean target
RM = rm -rf
 
#
# Compilation and linking
#

ep2: $(OBJ)
	@ echo 'Building binary using GCC linker: $@'
	$(CC) $^ -o $@.exe -lpthread
	@ echo 'Finished building binary: $@'
	@ echo ' '