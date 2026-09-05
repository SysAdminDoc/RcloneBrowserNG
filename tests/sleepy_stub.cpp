// A stand-in for the rclone binary that starts successfully, ignores its
// arguments, serves nothing, and stays alive well past the RC startup budget.
// async_helpers_test points the app at this to prove that bringing the rcd
// daemon up hands control back to the event loop instead of waiting.
#include <chrono>
#include <thread>

int main(int, char **) {
  std::this_thread::sleep_for(std::chrono::seconds(11));
  return 0;
}
