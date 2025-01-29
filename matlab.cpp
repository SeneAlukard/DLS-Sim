#include "MatlabDataArray.hpp"
#include "MatlabEngine.hpp"
#include <iostream>

int main() {
  try {
    auto matlabPtr = matlab::engine::startMATLAB(); // Start MATLAB Engine
    matlabPtr->eval(
        u"disp('Hello from MATLAB Engine API for C++!')"); // Run MATLAB command
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return -1;
  }
  return 0;
}
