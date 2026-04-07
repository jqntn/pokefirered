#include <cstring>

#include "unistd.h"

char* optarg = nullptr;
int optind = 1;
int opterr = 1;
int optopt = 0;

int
getopt(int argc, char* const argv[], const char* optstring)
{
  static int char_index = 1;
  const char* option = nullptr;
  const char* spec = nullptr;

  optarg = nullptr;

  if (optind >= argc) {
    return -1;
  }

  option = argv[optind];
  if (option == nullptr || option[0] != '-' || option[1] == '\0') {
    return -1;
  }

  if (std::strcmp(option, "--") == 0) {
    optind++;
    char_index = 1;
    return -1;
  }

  optopt = (unsigned char)option[char_index];
  spec = std::strchr(optstring, optopt);

  if (spec == nullptr) {
    if (option[++char_index] == '\0') {
      optind++;
      char_index = 1;
    }

    return '?';
  }

  if (spec[1] == ':') {
    if (option[char_index + 1] != '\0') {
      optarg = (char*)&option[char_index + 1];
      optind++;
      char_index = 1;
      return optopt;
    }

    if (optind + 1 >= argc) {
      optind++;
      char_index = 1;
      return '?';
    }

    optarg = argv[optind + 1];
    optind += 2;
    char_index = 1;
    return optopt;
  }

  if (option[++char_index] == '\0') {
    optind++;
    char_index = 1;
  }

  return optopt;
}
