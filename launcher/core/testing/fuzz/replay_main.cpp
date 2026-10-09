#include "reboot/testing/fuzz.hpp"

int main(int argc, char** argv) { return rb::testing::replay_corpus_main(argc, argv); }
