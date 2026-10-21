#include <cstdio>
extern "C" void InitializeDataSections();
int main() { InitializeDataSections(); std::puts("RESULT: OK"); return 0; }
