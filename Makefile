gram:gram.c
	$(gcc) gram.c -o gram -Wall -Wextra -pedantic -stdc=c99
	$(cp) gram.c copygram.c
