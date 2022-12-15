#include <iostream>
#include <random>

int main() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0, 1);

  for (int i = 0; i < 10; ++i) {
    std::cout << dis(gen) << std::endl;
  }

  return 0;
}


