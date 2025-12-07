#include "lib2.h"
#include "lib1.h"
#include <iostream>
void lib2_func() { lib1_func(); std::cout << "lib2" << std::endl; }
