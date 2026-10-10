#include "insn.h"
#include <cstdio>
#include <vector>
#include <fstream>
#include <sstream>
using namespace dspic;
int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb"); std::vector<uint32_t> img(0x78000 / 4);
  fread(img.data(), 4, img.size(), f); fclose(f);
  auto pw = [&](uint32_t a) -> uint32_t { uint32_t i = (a - 0x4000) / 2; return i < img.size() ? (img[i] & 0xFFFFFF) : 0; };
  std::ifstream in(argv[2]); std::string line;
  while (std::getline(in, line)) {
    std::stringstream ss(line); std::string a; ss >> a; uint32_t pc = std::stoul(a, nullptr, 16) / 2;
    Insn I = decode(pw(pc), pw(pc + 2), pc);
    printf("%x\t%d\t%s\t%06x\n", pc, I.words * 4, disasm(I, pc).c_str(), pw(pc));
  }
}
