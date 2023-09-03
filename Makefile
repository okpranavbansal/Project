kilo:kilo.c
	$(gcc) kilo.c -o kilo -Wall -Wextra -pedantic -stdc=c99
	$(cp) kilo.c copykilo.c
