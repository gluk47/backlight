all: backlight

%: %.cc
	F=$<; g++ -std=c++0x -o $${F%.cc} $<
