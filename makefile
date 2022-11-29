mmu: main.cpp
	clang++ -std=c++11 -stdlib=libc++ main.cpp -o mmu

clean:
	rm -f mmu *~




