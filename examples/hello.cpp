// C++ Hello World for ANCORATE AOS
// Compile with: aarch64-none-elf-g++ -nostdlib -ffreestanding hello.cpp -o
// hello

extern "C" void puts(const char *str);

extern "C" int main() {
  puts("Hello from C++!");
  puts("This is a C++ program running on ANCORATE AOS");
  return 0;
}
